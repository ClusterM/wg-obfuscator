#ifndef _WG_OBFUSCATOR_H_
#define _WG_OBFUSCATOR_H_

#include <stdint.h>
#include "uthash.h"

#define VERSION "1.0"
#define GIT_REPO "https://github.com/ClusterM/wg-obfuscator"

#define BUFFER_SIZE 2048
#define MAX_DUMMY_LENGTH 1024
#define MAX_EVENTS 1024

#define EPOLL_TIMEOUT           5000    // in milliseconds
#define HANDSHAKE_TIMEOUT       5       // seconds
#define MAX_CLIENTS             1024    // maximum number of clients
#define CLEANUP_INTERVAL        15      // seconds
#define IDLE_TIMEOUT            300     // seconds

// WireGuard handshake signature
static const uint8_t wg_signature_handshake[] = {0x01, 0x00, 0x00, 0x00};
static const uint8_t wg_signature_handshake_resp[] = {0x02, 0x00, 0x00, 0x00};

// WireGuard handshake lengths (for the latest versions)
#define HANDSHAKE_LENGTH 148
#define HANDSHAKE_RESP_LENGTH 92

// Logging levels
#define LL_ERROR        0
#define LL_WARN         1
#define LL_INFO         2
#define LL_DEBUG        3
#define LL_TRACE        4

typedef struct {
    struct sockaddr_in client_addr; // key
    struct sockaddr_in our_addr; // our address and port on the server connection
    int server_sock;
    struct timespec last_activity_time; // last time we received data from this client
    struct timespec last_handshake_request_time; // last time we received a handshake request from this client
    struct timespec last_handshake_time;
    uint8_t handshaked; // 1 if the client has completed the handshake, 0 otherwise
    UT_hash_handle hh;
} client_entry_t;

#endif
