#ifndef _WG_OBFUSCATOR_H_
#define _WG_OBFUSCATOR_H_

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include "uthash.h"

// uthash ships its own HASH_OOPS that calls fprintf(stderr,...)+exit(-1).
// Re-define it to additionally emit to syslog so malloc-failure messages are
// visible under systemd (where stderr may be disconnected).
#undef HASH_OOPS
#define HASH_OOPS(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    syslog(LOG_ERR, __VA_ARGS__); \
    exit(-1); \
} while (0)

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

#define WG_OBFUSCATOR_VERSION "1.5.2"
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

// Request 8 MiB of kernel UDP receive buffer per socket. The kernel doubles the
// value and clamps to net.core.rmem_max; on production systems you typically
// want `sysctl -w net.core.rmem_max=16777216` so the full 8 MiB is honored.
// Undersized rcvbuf is the main reason for UdpRcvbufErrors under bursts.
#define UDP_RCVBUF_BYTES        (8 * 1024 * 1024)

// Cap on the number of packets drained from a single fd during one epoll/poll
// event — prevents one busy fd from starving other fds or the idle-cleanup
// timer. The kernel re-notifies on the next wait if more data is queued.
#define DRAIN_BATCH_MAX         128

// Default instance name
#define DEFAULT_INSTANCE_NAME   "main"

// Logging levels
#define LL_ERROR        0
#define LL_WARN         1
#define LL_INFO         2
#define LL_DEBUG        3
#define LL_TRACE        4

// log_to_stderr: chosen once at startup from isatty(STDERR_FILENO).
//   1 → interactive — write to stderr (as before).
//   0 → daemon/redirected — write via syslog(3), which uses a SOCK_DGRAM Unix
//       socket to journald/syslogd and never blocks (the kernel drops on
//       queue-full rather than suspending the writer — exactly what the
//       original silent-hang bug needed). The syslog path is the ONLY path
//       guaranteed not to deadlock on a stalled reader.
extern int log_to_stderr;

#define log(level, fmt, ...) do {                                            \
    if (verbose >= (level)) {                                                \
        if (log_to_stderr) {                                                 \
            fprintf(stderr, "[%s][%c] " fmt "\n", section_name,              \
            (                                                                \
                  (level) == LL_ERROR ? 'E'                                  \
                : (level) == LL_WARN  ? 'W'                                  \
                : (level) == LL_INFO  ? 'I'                                  \
                : (level) == LL_DEBUG ? 'D'                                  \
                : (level) == LL_TRACE ? 'T'                                  \
                : '?'                                                        \
            ), ##__VA_ARGS__);                                               \
        } else {                                                             \
            syslog(                                                          \
                (level) == LL_ERROR ? LOG_ERR                                \
              : (level) == LL_WARN  ? LOG_WARNING                            \
              : (level) == LL_INFO  ? LOG_INFO                               \
              :                       LOG_DEBUG,                             \
                "[%s] " fmt, section_name, ##__VA_ARGS__);                   \
        }                                                                    \
    }                                                                        \
} while (0)

// trace() is the per-packet hex-dump facility: in the hot path it is invoked
// per byte (wg-obfuscator.c hex-dump loops) without a terminating newline,
// so it cannot be routed through the single-line syslog contract. Gate it
// behind DEBUG builds — release builds don't need it and it was the main
// source of stderr flooding in the original incident.
#ifdef DEBUG
#define trace(fmt, ...) if (log_to_stderr && verbose >= LL_TRACE) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define trace(fmt, ...) ((void)0)
#endif

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
    char static_bindings[10 * 1024];            // Static bindings as a string
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
