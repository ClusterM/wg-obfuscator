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

#define WG_OBFUSCATOR_VERSION "1.1"
#define WG_OBFUSCATOR_GIT_REPO "https://github.com/ClusterM/wg-obfuscator"

// Logging levels
#define LL_ERROR        0
#define LL_WARN         1
#define LL_INFO         2
#define LL_DEBUG        3
#define LL_TRACE        4

// Main parameters
// TODO: make these configurable via command line arguments or config file
#define BUFFER_SIZE             8*1024  // size of the buffer for receiving data from the clients and server
#define POLL_TIMEOUT            5000    // in milliseconds
#define HANDSHAKE_TIMEOUT       5000    // in milliseconds
#define MAX_CLIENTS             1024    // maximum number of clients
#define CLEANUP_INTERVAL        15000   // in milliseconds
#define IDLE_TIMEOUT            300000  // in milliseconds
#define MAX_DUMMY_LENGTH_TOTAL  1024    // maximum length of a packet after dummy data extension
#define MAX_DUMMY_LENGTH_HANDSHAKE  512 // maximum length of dummy data for handshake packets
#define MAX_DUMMY_LENGTH_DATA   4       // maximum length of dummy data for data packets

// WireGuard packet types
#define WG_TYPE_HANDSHAKE       0x01
#define WG_TYPE_HANDSHAKE_RESP  0x02
#define WG_TYPE_COOKIE          0x03
#define WG_TYPE_DATA            0x04

// Handshake directions
#define HANDSHAKE_DIRECTION_CLIENT_TO_SERVER 0
#define HANDSHAKE_DIRECTION_SERVER_TO_CLIENT 1

// Current obfuscation version
#define OBFUSCATION_VERSION     1
#define DEFAULT_INSTANCE_NAME   "main"

// Structure to hold client connection information
typedef struct {
    struct sockaddr_in client_addr;             // client address and port (key for the hash table)
    struct sockaddr_in our_addr;                // our address and port on the server connection
    long last_activity_time;                    // last time we received data from/to this client
    long last_handshake_request_time;           // last time we received a handshake request from/to this client
    long last_handshake_time;                   // last time we received a handshake response from/to this client
    int server_sock;                            // socket for the connection to the server    
    uint8_t version;                            // obfuscation version
    uint8_t handshaked          : 1;            // 1 if the client has completed the handshake, 0 otherwise
    uint8_t handshake_direction : 1;            // 1 if the handshake is from client to server, 0 if from server to client
    uint8_t is_static           : 1;            // 1 if this is a static binding entry, 0 otherwise
    UT_hash_handle hh;
} client_entry_t;

#endif
