#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "wg-obfuscator.h"
#include "stun.h"

static void rand_bytes(uint8_t* p, size_t n){ for(size_t i=0;i<n;i++) p[i]=rand()&0xFF; }

int stun_check_magic(const uint8_t *buf, size_t len) {
    if (!buf || len < 8) return 0;
    return !memcmp(buf+4, COOKIE_BE, 4);
}

uint16_t stun_peek_type(const uint8_t *buf){
    return (buf[0] << 8) | buf[1];
}

static int stun_write_header(uint8_t *b, uint16_t type, uint16_t mlen, const uint8_t txid[12]) {
    b[0]=type>>8;
    b[1]=type&0xFF;
    b[2]=mlen>>8;
    b[3]=mlen&0xFF;
    memcpy(b+4, COOKIE_BE, 4);
    memcpy(b+8,txid,12);
    return 20;
}

static int stun_attr_xor_mapped_addr(uint8_t *b, const struct sockaddr_in *src){
    // type,len
    b[0]=STUN_ATTR_XORMAPPED>>8;
    b[1]=STUN_ATTR_XORMAPPED&0xFF;
    b[2]=0;
    b[3]=8; // len=8 (family+port+addr)
    b[4]=0;
    b[5]=0x01; // family IPv4
    memcpy(b+6, &src->sin_port, 2);        // network-order bytes
    ((uint8_t*)b)[6] ^= COOKIE_BE[0];
    ((uint8_t*)b)[7] ^= COOKIE_BE[1];
    memcpy(b+8, &src->sin_addr.s_addr, 4); // network-order bytes
    ((uint8_t*)b)[8]  ^= COOKIE_BE[0];
    ((uint8_t*)b)[9]  ^= COOKIE_BE[1];
    ((uint8_t*)b)[10] ^= COOKIE_BE[2];
    ((uint8_t*)b)[11] ^= COOKIE_BE[3];
    return 12; // 4 hdr + 8 val
}

/*
static int stun_attr_software(uint8_t *b, const char *s){
    uint16_t n = (uint16_t)strlen(s);
    uint16_t pad = (4 - (n & 3)) & 3;
    if (b) {
        b[0] = STUN_ATTR_SOFTWARE >> 8; 
        b[1] = STUN_ATTR_SOFTWARE&0xFF;
        b[2] = n>>8;
        b[3] = n&0xFF;
        memcpy(b+4,s,n);
        if (pad) memset(b+4+n,0,pad);
    }
    return 4 + n + pad;
}
*/

static uint32_t crc32(const uint8_t *p, size_t n){
    uint32_t crc=~0u;
    for(size_t i=0;i<n;i++){
        crc ^= p[i];
        for(int k=0;k<8;k++)
            crc = (crc>>1) ^ (0xEDB88320u & (-(int)(crc&1)));
    }
    return ~crc;
}

static int stun_attr_fingerprint(uint8_t *pkt, size_t cur_len){
    uint8_t *b = pkt + cur_len;
    b[0]=STUN_ATTR_FINGERPR>>8;
    b[1]=STUN_ATTR_FINGERPR&0xFF;
    b[2]=0;
    b[3]=4;
    uint32_t fp = htonl(crc32(pkt, cur_len) ^ 0x5354554eu);
    memcpy(b+4, &fp, 4);
    return 8; // 4 hdr + 4 val
}

int stun_build_binding_request(uint8_t *out){
    if (BUFFER_SIZE < 20) return -1;
    uint8_t txid[12];
    rand_bytes(txid,12);
    stun_write_header(out, STUN_BINDING_REQ, 0, txid);
    size_t mlen = 0;
    // optional SOFTWARE
    //mlen += stun_attr_software(out+20+mlen, "wgo/1.0");
    mlen += stun_attr_fingerprint(out, 20+mlen);
    out[2] = (mlen>>8)&0xFF;
    out[3] = mlen&0xFF;
    return (int)(20+mlen);
}

static void stun_binding_request_send(int socket, const struct sockaddr_in *to) {
    uint8_t buffer[20];
    int len = stun_build_binding_request(buffer);
    if (len < 0) return;

    ssize_t sent;
    if (to) {
        sent = sendto(socket, buffer, len, 0, (const struct sockaddr *)to, sizeof(*to));
        log(LL_DEBUG, "Sending STUN binding request to %s:%d, result: %s", inet_ntoa(to->sin_addr), ntohs(to->sin_port), (sent == len) ? "success" : "failure");
    } else {
        sent = send(socket, buffer, len, 0);
        log(LL_DEBUG, "Sending STUN binding request to server, result: %s", (sent == len) ? "success" : "failure");
    }
    if (sent != len) {
        serror_level(LL_WARN, "STUN binding request");
    }
}

static int stun_build_binding_success(uint8_t *out,
                               const uint8_t txid[12],
                               const struct sockaddr_in *src) {
    if (BUFFER_SIZE < 20+12+8) return -1; // header + XOR-MAPPED-ADDRESS + fingerprint
    stun_write_header(out, STUN_BINDING_RESP, 0, (uint8_t*)txid);
    size_t mlen = 0;
    mlen += stun_attr_xor_mapped_addr(out+20+mlen, src);
    // optional SOFTWARE
    //mlen += stun_attr_software(out+20+mlen, "wgo/1.0");
    mlen += stun_attr_fingerprint(out, 20+mlen);
    out[2] = (mlen>>8)&0xFF;
    out[3] = mlen&0xFF;
    return (int)(20+mlen);
}

static int stun_wrap(uint8_t *buf, size_t data_len) {
    const size_t header_size = 20;      // STUN header
    const size_t attr_header = 4;       // type+length
    size_t total_add = header_size + attr_header;
    size_t mlen = 0;

    if (data_len + total_add > BUFFER_SIZE) return -1;

    memmove(buf + total_add, buf, data_len);

    uint8_t txid[12];
    rand_bytes(txid,12);
    mlen += stun_write_header(buf, STUN_TYPE_DATA_IND, 0, txid);

    buf[mlen] = STUN_ATTR_DATA >> 8;
    buf[mlen+1] = STUN_ATTR_DATA & 0xFF;
    buf[mlen+2] = data_len >> 8;
    buf[mlen+3] = data_len & 0xFF;

    return header_size + attr_header + data_len;
}

static int stun_unwrap(uint8_t *buf, size_t len) {
    if (len < 24) return -1; // header+attr

    uint16_t msg_type = (buf[0] << 8) | buf[1];
    if (msg_type != STUN_TYPE_DATA_IND) return -1;

    uint16_t msg_len = (buf[2] << 8) | buf[3];
    if (msg_len + 20 > len) return -1;

    uint16_t attr_type = (buf[20] << 8) | buf[21];
    if (attr_type != STUN_ATTR_DATA) return -1;

    uint16_t data_len = (buf[22] << 8) | buf[23];
    if (data_len + 24 > len) return -1;

    memmove(buf, buf + 24, data_len);

    return data_len;
}

static int stun_on_data_received(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                direction_t direction,
                                const struct sockaddr_in *src_addr,
                                const struct sockaddr_in *dest_addr,
                                send_data_callback_t send_back_callback,
                                send_data_callback_t send_forward_callback) {
    if (!stun_check_magic(buffer, length)) {
            return -EINVAL;
    }

    uint16_t stun_type = stun_peek_type(buffer);

    switch (stun_type) {
    case STUN_BINDING_REQ: {
        // Received STUN Binding Request from client, send Binding Success Response
        log(LL_DEBUG, "Received STUN Binding Request from %s:%d", inet_ntoa(src_addr->sin_addr), ntohs(src_addr->sin_port));
        uint8_t txid[12];
        memcpy(txid, buffer + 8, 12);
        int resp_len = stun_build_binding_success(buffer, txid, src_addr);
        if (resp_len > 0) {
        // int sent = sendto(listen_sock, buffer, resp_len, 0, (struct sockaddr *)&sender_addr, sizeof(sender_addr));
            int sent = send_back_callback(buffer, resp_len);
            if (sent != resp_len) {
                serror_level(LL_DEBUG, "sendto STUN response to %s:%d", inet_ntoa(src_addr->sin_addr), ntohs(src_addr->sin_port));
            } else {
                log(LL_DEBUG, "Sent STUN Binding Success Response (%d bytes) to %s:%d", resp_len, inet_ntoa(src_addr->sin_addr), ntohs(src_addr->sin_port));
            }
        } else {
            log(LL_DEBUG, "Failed to build STUN Binding Success Response");
        }
        return 0;
    }
    case STUN_BINDING_RESP:
        log(LL_DEBUG, "Received STUN Binding Success Response from %s:%d, ignoring", inet_ntoa(src_addr->sin_addr), ntohs(src_addr->sin_port));
        return 0;
    case STUN_TYPE_DATA_IND:
        length = stun_unwrap(buffer, length);
        if (length < 0) {
            log(LL_DEBUG, "Failed to unwrap STUN Data Indication from %s:%d", inet_ntoa(src_addr->sin_addr), ntohs(src_addr->sin_port));
            return length;
        }
        log(LL_DEBUG, "Unwrapped STUN Data Indication from %s:%d (%d bytes)", inet_ntoa(src_addr->sin_addr), ntohs(src_addr->sin_port), length);
        return length;
    default:
        log(LL_DEBUG, "Received unknown STUN type %04X from %s:%d, ignoring", stun_type, inet_ntoa(src_addr->sin_addr), ntohs(src_addr->sin_port));
        return 0;
    }
}

static int stun_on_data_wrap(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                direction_t direction,
                                const struct sockaddr_in *src_addr,
                                const struct sockaddr_in *dest_addr,
                                send_data_callback_t send_back_callback,
                                send_data_callback_t send_forward_callback) {
    return stun_wrap(buffer, length);
}

static void stun_on_handshake_req(obfuscator_config_t *config,
                                client_entry_t *client,
                                direction_t direction,
                                const struct sockaddr_in *src_addr,
                                const struct sockaddr_in *dest_addr,
                                send_data_callback_t send_back_callback,
                                send_data_callback_t send_forward_callback) {
    uint8_t buffer[20];
    int len = stun_build_binding_request(buffer);
    if (len < 0) return;

    if (send_forward_callback(buffer, len) != len) {
        log(LL_WARN, "can't send STUN binding request to %s:%d", inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port));
    }
}

static void stun_on_timer(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr) {
    if (client->client_obfuscated) {
        stun_binding_request_send(listen_sock, &client->client_addr);
    }
    if (client->server_obfuscated) {
        stun_binding_request_send(client->server_sock, NULL);
    }
}

struct send_ctx {
    int listen_sock;
    struct sockaddr_in *sender_addr;
    int server_sock;
};

static _Thread_local struct send_ctx *g_send_ctx; // thread-local to avoid races

static int send_to_client_cb(uint8_t *buffer, int length) {
    struct send_ctx *c = g_send_ctx;
    return sendto(c->listen_sock, buffer, length, 0, (struct sockaddr *)c->sender_addr, sizeof(*c->sender_addr));
}

static int send_to_server_cb(uint8_t *buffer, int length) {
    struct send_ctx *c = g_send_ctx;
    return send(c->server_sock, buffer, length, 0);
}

void masking_on_handshake_req_from_client(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr) {
    struct send_ctx ctx = { listen_sock, &client->client_addr, client->server_sock };
    g_send_ctx = &ctx; // set context for this thread

    stun_on_handshake_req(config, client, DIR_CLIENT_TO_SERVER, client_addr, server_addr, send_to_client_cb, send_to_server_cb);
}

void masking_on_handshake_req_from_server(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr) {
    struct send_ctx ctx = { listen_sock, &client->client_addr, client->server_sock };
    g_send_ctx = &ctx; // set context for this thread

    stun_on_handshake_req(config, client, DIR_SERVER_TO_CLIENT, server_addr, client_addr, send_to_server_cb, send_to_client_cb);
}

int masking_unwrap_from_client(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client, // can be NULL!
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr,
                                masking_type_t *masking_type_out) {
    struct send_ctx ctx = { listen_sock, client_addr, client ? client->server_sock : 0 };
    g_send_ctx = &ctx; // set context for this thread

    int r = stun_on_data_received(buffer, length, config, client, DIR_CLIENT_TO_SERVER, client_addr, server_addr, send_to_client_cb, client ? send_to_server_cb : NULL);
    if (r < 0) {
        return length;
    } else {
        *masking_type_out = MASKING_STUN;
        return r;
    };
}

int masking_unwrap_from_server(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr) {
    struct send_ctx ctx = { listen_sock, &client->client_addr, client->server_sock };
    g_send_ctx = &ctx; // set context for this thread

    int r = stun_on_data_received(buffer, length, config, client, DIR_SERVER_TO_CLIENT, server_addr, client_addr, send_to_server_cb, send_to_client_cb);
    if (r < 0) {
        return length;
    } else {
        client->masking_type = MASKING_STUN;
        return r;
    };
}

int masking_data_wrap_to_client(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr) {
    struct send_ctx ctx = { listen_sock, &client->client_addr, client->server_sock };
    g_send_ctx = &ctx; // set context for this thread

    return stun_on_data_wrap(buffer, length, config, client, DIR_SERVER_TO_CLIENT, server_addr, &client->client_addr, send_to_server_cb, send_to_client_cb);
}

int masking_data_wrap_to_server(uint8_t *buffer, int length,
                                obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *server_addr) {
    struct send_ctx ctx = { listen_sock, &client->client_addr, client->server_sock };
    g_send_ctx = &ctx; // set context for this thread

    return stun_on_data_wrap(buffer, length, config, client, DIR_CLIENT_TO_SERVER, &client->client_addr, server_addr, send_to_client_cb, send_to_server_cb);
}

void masking_on_timer(obfuscator_config_t *config,
                                client_entry_t *client,
                                int listen_sock,
                                struct sockaddr_in *client_addr,
                                struct sockaddr_in *server_addr) {
    stun_on_timer(config, client, listen_sock, client_addr, server_addr);
}