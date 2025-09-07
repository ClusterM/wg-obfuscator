#ifndef _WG_OBFUSCATOR_H_
#define _WG_OBFUSCATOR_H_

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include "uthash.h"

// on Linux, use epoll for better performance
#ifdef __linux__
#define USE_EPOLL
#endif

#ifdef USE_EPOLL
#include <sys/epoll.h>
#define MAX_EVENTS              1024
#else
#include <poll.h>
#endif

#define WG_OBFUSCATOR_VERSION "1.3"
#define WG_OBFUSCATOR_GIT_REPO "https://github.com/ClusterM/wg-obfuscator"

#define LL_DEFAULT      LL_INFO  // default logging level

// Main parameters
// TODO: make these configurable via command line arguments or config file
#define BUFFER_SIZE                     65535   // size of the buffer for receiving data from the clients and server
#define POLL_TIMEOUT                    5000    // in milliseconds
#define HANDSHAKE_TIMEOUT               5000    // in milliseconds
#define ITERATE_INTERVAL                1000    // in milliseconds
#define MAX_DUMMY_LENGTH_TOTAL          1024    // maximum length of a packet after dummy data extension
#define MAX_DUMMY_LENGTH_HANDSHAKE      512     // maximum length of dummy data for handshake packets

#define MAX_CLIENTS_DEFAULT             1024    // maximum number of clients
#define IDLE_TIMEOUT_DEFAULT            300000  // in milliseconds
#define MAX_DUMMY_LENGTH_DATA_DEFAULT   4       // maximum length of dummy data for data packets

// Default instance name
#define DEFAULT_INSTANCE_NAME   "main"

// Logging levels
#define LL_ERROR        0
#define LL_WARN         1
#define LL_INFO         2
#define LL_DEBUG        3
#define LL_TRACE        4

#define log(level, fmt, ...) { if (verbose >= (level))       \
    fprintf(stderr, "[%s][%c] " fmt "\n", section_name,      \
    (                                                               \
          (level) == LL_ERROR ? 'E'                                 \
        : (level) == LL_WARN  ? 'W'                                 \
        : (level) == LL_INFO  ? 'I'                                 \
        : (level) == LL_DEBUG ? 'D'                                 \
        : (level) == LL_TRACE ? 'T'                                 \
        : '?'                                                       \
    ), ##__VA_ARGS__);                                              \
}
#define trace(fmt, ...) if (verbose >= LL_TRACE) fprintf(stderr, fmt, ##__VA_ARGS__)
#define serror_level(level, fmt, ...) log(level, fmt " - %s (%d)", ##__VA_ARGS__, strerror(errno), errno)
#define serror(fmt, ...) serror_level(LL_ERROR, fmt, ##__VA_ARGS__)

// Direction of... something
typedef enum {
    DIR_CLIENT_TO_SERVER = 0,
    DIR_SERVER_TO_CLIENT = 1,
} direction_t;

struct masking_handler; // forward declaration
typedef struct masking_handler masking_handler_t;

// Structure to hold obfuscator configuration
typedef struct {
    int listen_port;                            // Listening port for the obfuscator
    char forward_host_port[256];                // Host and port to forward the data to
    char xor_key[256];                          // Key for obfuscation
    char client_interface[256];                 // Client interface as a string
    char static_bindings[2048];                 // Static bindings as a string
    int max_clients;                            // Maximum number of clients
    long idle_timeout;                          // Idle timeout in milliseconds
    int max_dummy_length_data;                  // Maximum length of dummy data for data packets
    uint32_t fwmark;                            // Firewall mark
    masking_handler_t *masking_handler;         // Masking handler to use

    uint8_t listen_port_set;                    // 1 if the listen port is set, 0 otherwise
    uint8_t forward_host_port_set;              // 1 if the forward host and port are set, 0 otherwise
    uint8_t xor_key_set;                        // 1 if the XOR key is set, 0 otherwise
    uint8_t client_interface_set;               // 1 if the client interface is set, 0 otherwise
    uint8_t static_bindings_set;                // 1 if the static bindings are set, 0 otherwise
    uint8_t masking_handler_set;                // 1 if the masking handler is set, 0 otherwise
} obfuscator_config_t;

// Structure to hold client connection information
typedef struct {
    struct sockaddr_in client_addr;             // client address and port (key for the hash table)
    struct sockaddr_in our_addr;                // our address and port on the server connection
    long last_activity_time;                    // last time we received data from/to this client
    long last_handshake_request_time;           // last time we received a handshake request from/to this client
    long last_handshake_time;                   // last time we received a handshake response from/to this client
    long last_masking_timer_time;               // last time we called the masking timer handler for this client
    int server_sock;                            // socket for the connection to the server    
    uint8_t version;                            // obfuscation version
    masking_handler_t *masking_handler;         // masking handler in use
    uint8_t handshaked          : 1;            // 1 if the handshake is complete, 0 otherwise
    uint8_t handshake_direction : 1;            // 1 if the handshake is from client to server, 0 if from server to client
    uint8_t client_obfuscated   : 1;            // 1 if the client is obfuscated, 0 otherwise
    uint8_t server_obfuscated   : 1;            // 1 if the server is obfuscated, 0 otherwise
    uint8_t is_static           : 1;            // 1 if this is a static binding entry, 0 otherwise
    UT_hash_handle hh;
} client_entry_t;

// Verbosity level
extern int verbose;
// Section name (for multiple instances)
extern char section_name[256];

void print_version(void);

#endif
