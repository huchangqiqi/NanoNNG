#include "nng/supplemental/nanolib/blf.h"
#include "nng/supplemental/nanolib/log.h"
#include "queue.h"
#include <Vector/BLF.h>
#include <assert.h>
#include <atomic>
#include <bitset>
#include <codecvt>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <inttypes.h>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
using namespace std;

#define UINT64_MAX_DIGITS 20
#define _Atomic(X) std::atomic<X>
static atomic_bool is_available = { false };
#define WAIT_FOR_AVAILABLE    \
	while (!is_available) \
		nng_msleep(10);
static conf_blf *g_conf = NULL;

#define DO_IT_IF_NOT_NULL(func, arg1, arg2) \
	if (arg1) {                         \
		func(arg1, arg2);           \
	}

#define FREE_IF_NOT_NULL(free, size) DO_IT_IF_NOT_NULL(nng_free, free, size)

#define json_read_num(structure, field, key, jso)                       \
	do {                                                                  \
		cJSON *jso_key = cJSON_GetObjectItem(jso, key);               \
		if (NULL == jso_key) {                                        \
			log_debug("Config %s is not set, use default!", key); \
			break;                                                \
		}                                                             \
		if (cJSON_IsNumber(jso_key)) {                                \
			if (jso_key->valuedouble > 0)                         \
				(structure)->field = jso_key->valuedouble;    \
		}                                                             \
	} while (0);



CircularQueue   blf_queue;
CircularQueue   blf_file_queue;
pthread_mutex_t blf_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  blf_queue_not_empty = PTHREAD_COND_INITIALIZER;

static bool
directory_exists(const std::string &directory_path)
{
	struct stat buffer;
	return (stat(directory_path.c_str(), &buffer) == 0 &&
	    S_ISDIR(buffer.st_mode));
}

static bool
create_directory(const std::string &directory_path)
{
	int status = mkdir(
	    directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	return (status == 0);
}

blf_file_range *
blf_file_range_alloc(uint32_t start_idx, uint32_t end_idx, char *filename)
{
	blf_file_range *range = new blf_file_range;
	range->start_idx      = start_idx;
	range->end_idx        = end_idx;
	range->filename       = nng_strdup(filename);
	return range;
}

void
blf_file_range_free(blf_file_range *range)
{
	if (range) {
		FREE_IF_NOT_NULL(range->filename, strlen(range->filename));
		delete range;
	}
}

blf_object *
blf_object_alloc(uint64_t *keys, uint8_t **darray, uint32_t *dsize,
    uint32_t size, nng_aio *aio, void *arg)
{
	blf_object *elem    = new blf_object;
	elem->keys          = keys;
	elem->darray        = darray;
	elem->dsize         = dsize;
	elem->size          = size;
	elem->aio           = aio;
	elem->arg           = arg;
	elem->ranges        = new blf_file_ranges;
	elem->ranges->range = NULL;
	elem->ranges->start = 0;
	elem->ranges->size  = 0;
	return elem;
}

void
blf_object_free(blf_object *elem)
{
	if (elem) {
		FREE_IF_NOT_NULL(elem->keys, elem->size);
		FREE_IF_NOT_NULL(elem->dsize, elem->size);
		nng_aio_set_prov_data(elem->aio, elem->arg);
		nng_aio_set_output(elem->aio, 1, elem->ranges);
		uint32_t *szp = (uint32_t *) malloc(sizeof(uint32_t));
		*szp          = elem->size;
		nng_aio_set_msg(elem->aio, (nng_msg *) szp);
		DO_IT_IF_NOT_NULL(nng_aio_finish_sync, elem->aio, 0);
		FREE_IF_NOT_NULL(elem->darray, elem->size);
		for (int i = 0; i < elem->ranges->size; i++) {
			blf_file_range_free(elem->ranges->range[i]);
		}
		free(elem->ranges->range);
		delete elem->ranges;
		delete elem;
	}
}

static char *
get_file_name(conf_blf *conf, uint64_t key_start, uint64_t key_end)
{
	char *file_name = NULL;
	char *dir       = conf->dir;
	char *prefix    = conf->file_name_prefix;

	file_name = (char *) malloc(strlen(prefix) + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}

	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".blf", dir, prefix,
	    key_start, key_end);
	ENQUEUE(blf_file_queue, file_name);
	return file_name;
}

static int
compute_new_index(blf_object *obj, uint32_t index, uint32_t file_size)
{
	uint64_t size = 0;
	uint32_t new_index;
	for (new_index = index; size < file_size && new_index < obj->size - 1;
	     new_index++) {
		size += obj->dsize[new_index];
	}
	return new_index;
}

static int
remove_old_file(void)
{
	char *filename = (char *) DEQUEUE(blf_file_queue);
	if (remove(filename) == 0) {
		log_debug("File '%s' removed successfully.\n", filename);
	} else {
		log_error("Error removing the file %s", filename);
		return -1;
	}

	free(filename);
	return 0;
}

void
update_blf_file_ranges(conf_blf *conf, blf_object *elem, blf_file_range *range)
{
	if (elem->ranges->size != conf->file_count) {
		elem->ranges->range =
		    (blf_file_range **) realloc(elem->ranges->range,
		        sizeof(blf_file_range *) * (++elem->ranges->size));
		elem->ranges->range[elem->ranges->size - 1] = range;
	} else {
		// Free old ranges and insert new ranges
		// update start index
		blf_file_range_free(elem->ranges->range[elem->ranges->start]);
		elem->ranges->range[elem->ranges->start] = range;
		elem->ranges->start++;
		elem->ranges->start %= elem->ranges->size;
	}
}

void
read_binary_data(const std::string &inputString, unsigned int inputSize,
    array<uint8_t, 8> &data)
{
	std::vector<unsigned char> binaryData;
	for (size_t i = 0; 2 * i < inputString.length(); i++) {
		std::istringstream iss(inputString.substr(2 * i, 2));
		unsigned int       value;
		iss >> std::hex >> value;
		data[i] = (static_cast<unsigned char>(value));
	}
}
