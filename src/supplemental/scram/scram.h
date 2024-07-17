//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef NNG_SUPP_SCRAM_H
#define NNG_SUPP_SCRAM_H

uint8_t *scram_client_first_msg(const char *username);

char *scram_handle_client_first_msg(const char *msg, int len, int iteration_cnt);
int scram_handle_server_first_msg();
int scram_handle_client_final_msg();
int scram_handle_server_final_msg();

#endif
