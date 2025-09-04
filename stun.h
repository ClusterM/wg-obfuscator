#ifndef _STUN_H_
#define _STUN_H_

#include <stdint.h>
#include <netinet/in.h>
#include "wg-obfuscator.h"

static const uint8_t COOKIE_BE[4] = {0x21,0x12,0xA4,0x42};
#define STUN_TYPE_DATA_IND      0x0115
#define STUN_ATTR_DATA          0x0013
#define STUN_BINDING_REQ        0x0001
#define STUN_BINDING_RESP       0x0101
#define STUN_ATTR_XORMAPPED     0x0020
#define STUN_ATTR_SOFTWARE      0x8022
#define STUN_ATTR_FINGERPR      0x8028

typedef int (*send_data_callback_t)(uint8_t *buffer, int length);

int masking_unwrap_from_client(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr,
                                masking_type_t *masking_type_out);

int masking_unwrap_from_server(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr);

void masking_on_handshake_req_from_client(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr);

void masking_on_handshake_req_from_server(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr);

int masking_data_wrap_to_client(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr);

int masking_data_wrap_to_server(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr);

void masking_on_timer(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr);
#endif // _STUN_H_
