#ifndef _WG_OBFUSCATOR_H_
#define _WG_OBFUSCATOR_H_

// on Linux, use epoll for better performance
#ifdef __linux__
#define USE_EPOLL
#endif

#include <stdint.h>
#include "uthash.h"

#ifdef USE_EPOLL
#include <sys/epoll.h>
#define MAX_EVENTS              1024
#else
#include <poll.h>
#endif

#define VERSION "1.0"
#define GIT_REPO "https://github.com/ClusterM/wg-obfuscator"

#define BUFFER_SIZE             2048
#define OBFUSCATION_VERSION     1 // current obfuscation version

#define MAX_DUMMY_LENGTH_TOTAL  1024    // maximum length of a packet after dummy data extension
#define MAX_DUMMY_LENGTH_HANDSHAKE  512 // maximum length of dummy data for handshake packets
#define MAX_DUMMY_LENGTH_DATA   4       // maximum length of dummy data for data packets


#define POLL_TIMEOUT            5000    // in milliseconds
#define HANDSHAKE_TIMEOUT       5       // seconds
#define MAX_CLIENTS             1024    // maximum number of clients
#define CLEANUP_INTERVAL        15      // seconds
#define IDLE_TIMEOUT            300     // seconds

// WireGuard packet types
#define WG_TYPE_HANDSHAKE       0x01
#define WG_TYPE_HANDSHAKE_RESP  0x02
#define WG_TYPE_COOKIE          0x03
#define WG_TYPE_DATA            0x04

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
    struct timespec last_activity_time; // last time we received data from this client
    struct timespec last_handshake_request_time; // last time we received a handshake request from this client
    struct timespec last_handshake_time;
    int server_sock;
    uint8_t handshaked; // 1 if the client has completed the handshake, 0 otherwise
    uint8_t version; // obfuscation version
    UT_hash_handle hh;
} client_entry_t;

#endif
