#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include "wg-obfuscator.h"
#include "masking.h"
#include "masking_handlers.h"

static _Thread_local struct {
    int listen_sock;
    struct sockaddr_in *sender_addr;
    int server_sock;
} g_send_ctx;

static ssize_t send_to_client_cb(uint8_t *buffer, int length) {
    return sendto(g_send_ctx.listen_sock, buffer, length, 0, (struct sockaddr *)g_send_ctx.sender_addr, sizeof(*g_send_ctx.sender_addr));
}

static ssize_t send_to_server_cb(uint8_t *buffer, int length) {
    return send(g_send_ctx.server_sock, buffer, length, 0);
}

masking_handler_t * get_masking_handler_by_name(const char *name) {
    if (!name) return NULL;

    for (int i = 0; masking_handlers[i]; ++i) {
        char handler_name_lower[sizeof(masking_handlers[i]->name)];
        snprintf(handler_name_lower, sizeof(handler_name_lower), "%s", masking_handlers[i]->name);
        handler_name_lower[sizeof(handler_name_lower) - 1] = 0;
        for (char *p = handler_name_lower; *p; ++p) *p = tolower((unsigned char)*p);

        if (strcmp(name, handler_name_lower) == 0) {
            return masking_handlers[i];
        }
    }
    return NULL;
}

void masking_on_handshake_req_from_client(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr) {
    if (!client->masking_handler || !client->masking_handler->on_handshake_req) {
        return;
    }

    g_send_ctx.listen_sock = listen_sock;
    g_send_ctx.sender_addr = &client->client_addr;
    g_send_ctx.server_sock = client->server_sock;
    client->masking_handler->on_handshake_req(config, client, DIR_CLIENT_TO_SERVER, client_addr, server_addr, send_to_client_cb, send_to_server_cb);
}

void masking_on_handshake_req_from_server(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr) {
    if (!client->masking_handler || !client->masking_handler->on_handshake_req) {
        return;
    }

    g_send_ctx.listen_sock = listen_sock;
    g_send_ctx.sender_addr = &client->client_addr;
    g_send_ctx.server_sock = client->server_sock;
    client->masking_handler->on_handshake_req(config, client, DIR_SERVER_TO_CLIENT, server_addr, client_addr, send_to_server_cb, send_to_client_cb);
}

int masking_unwrap_from_client(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client, // can be NULL!
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr,
                                masking_handler_t **masking_handler_out) {
    g_send_ctx.listen_sock = listen_sock;
    g_send_ctx.sender_addr = client_addr;
    g_send_ctx.server_sock = client ? client->server_sock : 0;

    if (!client && !config->masking_handler_set) {
        // Brueteforce detection of masking type if no client entry and no default masking handler
        for (int i = 0; masking_handlers[i]; ++i) {
            int r = masking_handlers[i]->on_data_unwrap(buffer, length, config, NULL, DIR_CLIENT_TO_SERVER, client_addr, server_addr, send_to_client_cb, send_to_server_cb);
            if (r >= 0) {
                // Found a matching masking handler
                log(LL_TRACE, "Autodetected masking handler for packet from %s:%d: %s", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), masking_handlers[i]->name);
                if (masking_handler_out) {
                    *masking_handler_out = masking_handlers[i];
                }
                return r;
            }
        }

        // No matching masking handler found
        if (masking_handler_out) {
            *masking_handler_out = NULL;
        }
        return length;
    }

    masking_handler_t *handler = client ? client->masking_handler : config->masking_handler;
    if (!handler || !handler->on_data_unwrap) {
        return length; // no masking handler, nothing to do
    }

    return handler->on_data_unwrap(buffer, length, config, client, DIR_CLIENT_TO_SERVER, client_addr, server_addr, send_to_client_cb, client ? send_to_server_cb : NULL);
}

int masking_unwrap_from_server(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr) {
    if (!client->masking_handler || !client->masking_handler->on_data_unwrap) {
        return length; // no masking handler, nothing to do
    }

    g_send_ctx.listen_sock = listen_sock;
    g_send_ctx.sender_addr = &client->client_addr;
    g_send_ctx.server_sock = client->server_sock;
    return client->masking_handler->on_data_unwrap(buffer, length, config, client, DIR_SERVER_TO_CLIENT, server_addr, &client->client_addr, send_to_server_cb, send_to_client_cb);
}

int masking_data_wrap_to_client(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr) {
    if (!client->masking_handler || !client->masking_handler->on_data_wrap) {
        return length; // no masking handler, nothing to do
    }

    g_send_ctx.listen_sock = listen_sock;
    g_send_ctx.sender_addr = &client->client_addr;
    g_send_ctx.server_sock = client->server_sock;
    return client->masking_handler->on_data_wrap(buffer, length, config, client, DIR_SERVER_TO_CLIENT, server_addr, &client->client_addr, send_to_server_cb, send_to_client_cb);
}

int masking_data_wrap_to_server(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr) {
    if (!client->masking_handler || !client->masking_handler->on_data_wrap) {
        return length; // no masking handler, nothing to do
    }

    g_send_ctx.listen_sock = listen_sock;
    g_send_ctx.sender_addr = &client->client_addr;
    g_send_ctx.server_sock = client->server_sock;
    return client->masking_handler->on_data_wrap(buffer, length, config, client, DIR_CLIENT_TO_SERVER, &client->client_addr, server_addr, send_to_client_cb, send_to_server_cb);
}

void masking_on_timer(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr) {
    if (!client->masking_handler || !client->masking_handler->on_timer) {
        return;
    }

    g_send_ctx.listen_sock = listen_sock;
    g_send_ctx.sender_addr = &client->client_addr;
    g_send_ctx.server_sock = client->server_sock;
    client->masking_handler->on_timer(config, client, &client->client_addr, server_addr, send_to_client_cb, send_to_server_cb);
}
