#ifndef NNG_SUPP_QUIC_API_H
#define NNG_SUPP_QUIC_API_H

#include "core/nng_impl.h"
#include "nng/nng.h"

typedef struct quic_dialer quic_dialer;

extern int nni_quic_listener_alloc(nng_stream_listener **, const nni_url *);
extern int nni_quic_dialer_alloc(nng_stream_dialer **, const nni_url *);

typedef struct nni_quic_dialer nni_quic_dialer;

extern int  nni_quic_dialer_init(void *);
extern void nni_quic_dialer_fini(void *);
extern void nni_quic_dial(void *, const char *, nni_aio *);
extern void nni_quic_dialer_close(void *);

typedef struct nni_quic_conn nni_quic_conn;

extern int  nni_msquic_quic_alloc(nni_quic_conn **, nni_quic_dialer *);
extern void nni_msquic_quic_init(nni_quic_conn *, nni_posix_pfd *);
extern void nni_msquic_quic_start(nni_quic_conn *, int, int);
// extern void nni_msquic_quic_dialer_rele(nni_quic_dialer *);

/*
 * Note.
 *
 * qsock is the handle of a quic connection.
 * Which can NOT be used to write or read.
 *
 * qpipe is the handle of a quic stream.
 * All qpipes should be were closed before disconnecting qsock.
 */

// Enable MsQuic
extern void quic_open();
// Disable MsQuic and free
extern void quic_close();

// Enable quic protocol for nng
extern void quic_proto_open(nni_proto *proto);
// Disable quic protocol for nng
extern void quic_proto_close();
// Set global configuration for quic protocol
extern void quic_proto_set_bridge_conf(void *arg);

// Establish a quic connection to target url. Return 0 if success.
// And the handle of connection(qsock) would pass to callback .pipe_init(,qsock,)
// Or the connection is failed in eastablishing.
extern int quic_connect_ipv4(const char *url, nni_sock *sock, uint32_t *index, void **qsockp);
// Close connection
extern int quic_disconnect(void *qsock, void *qpipe);
// set close flag of qsock to true
extern void quic_sock_close(void *qsock);
// Create a qpipe and open it
extern int quic_pipe_open(void *qsock, void **qpipe, void *mqtt_pipe);
// get disconnect reason code from QUIC transport
extern uint8_t quic_sock_disconnect_code(void *arg);
// Receive msg from a qpipe
extern int quic_pipe_recv(void *qpipe, nni_aio *raio);
// Send msg to a qpipe
extern int quic_pipe_send(void *qpipe, nni_aio *saio);
extern int quic_aio_send(void *arg, nni_aio *aio);
// Close a qpipe and free it
extern int quic_pipe_close(void *qpipe, uint8_t *code);

#endif
