#ifndef _MASKING_H_
#define _MASKING_H_

#include <stdint.h>
#include <netinet/in.h>
#include "wg-obfuscator.h"

typedef ssize_t (*send_data_callback_t)(uint8_t *buffer, int length);

typedef void (*masking_event_handler_t)(obfuscator_config_t *config,
                                client_entry_t *client,
                                direction_t direction,
                                const struct sockaddr_in *src_addr,
                                const struct sockaddr_in *dest_addr,
                                send_data_callback_t send_back_callback,
                                send_data_callback_t send_forward_callback);

typedef int (*masking_data_handler_t)(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                direction_t direction,
                                const struct sockaddr_in *src_addr,
                                const struct sockaddr_in *dest_addr,
                                send_data_callback_t send_back_callback,
                                send_data_callback_t send_forward_callback);

typedef void (*masking_timer_handler_t)(obfuscator_config_t *config,
                                client_entry_t *client,
                                const struct sockaddr_in *client_addr,
                                const struct sockaddr_in *server_addr,
                                send_data_callback_t send_to_client_callback,
                                send_data_callback_t send_to_server_callback);

struct masking_handler {
    char name[32];
    masking_event_handler_t on_handshake_req;
    masking_data_handler_t on_data_wrap;
    masking_data_handler_t on_data_unwrap;
    masking_timer_handler_t on_timer;
    uint32_t timer_interval_s;
};
typedef struct masking_handler masking_handler_t;

masking_handler_t * get_masking_handler_by_name(const char *name);

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

int masking_unwrap_from_client(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr,
                                masking_handler_t **masking_handler_out);

int masking_unwrap_from_server(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr);

void masking_on_timer(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr);

#endif // _MASKING_H_
