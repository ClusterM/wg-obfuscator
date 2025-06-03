#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <argp.h>
#include <stdarg.h>
#include "wg-obfuscator.h"
#include "uthash.h"
#include "commit.h"

#define log(level, fmt, ...) { if (verbose >= (level))          \
    fprintf(stderr, "[%s][%c] " fmt "\n", section_name,         \
    (                                                           \
          (level) == LL_ERROR ? 'E'                               \
        : (level) == LL_WARN  ? 'W'                               \
        : (level) == LL_INFO  ? 'I'                               \
        : (level) == LL_DEBUG ? 'D'                               \
        : (level) == LL_TRACE ? 'T'                               \
        : '?'                                                   \
    ), ##__VA_ARGS__);                                          \
}
#define trace(fmt, ...) if (verbose >= LL_TRACE) fprintf(stderr, fmt, ##__VA_ARGS__)

// Listening socket for receiving data from the clients
static int listen_sock = 0;
// Hash table for client connections
static client_entry_t *conn_table = NULL;

#ifdef USE_EPOLL
    static int epfd = 0;
#endif

// Main parameters (TODO: IPv6?)
static char section_name[256] = DEFAULT_INSTANCE_NAME;
// Listening port for the obfuscator
static int listen_port = -1;
// Host and port to forward the data to
static char forward_host_port[256] = {0};
// Key for obfuscation
static char xor_key[256] = {0};
// Client interface
static char client_interface[256] = {0};
// Static bindings for two-way mode
static char static_bindings[2048] = {0};
// Verbosity level
static char verbose_str[256] = {0};
static int verbose = LL_INFO;

/**
 * @brief Prints an error message related to a specific section.
 *
 * This function prints an error message prefixed by the provided string and section name.
 * Additional arguments can be provided for formatted output.
 *
 * @param str      The error message prefix.
 * @param section  The name of the section related to the error.
 * @param ...      Additional arguments for formatting the error message.
 */
static void perror_sect(char *str, char* section, ...)
{
    char buf[512];
    va_list args;
    va_start(args, section);
    vsnprintf(buf, sizeof(buf), str, args);
    va_end(args);

    char msg[1024];
    snprintf(msg, sizeof(msg), "[%s][E] %s", section, buf);
    perror(msg);
}

#define serror(x, ...) perror_sect(x, section_name, ##__VA_ARGS__)


/**
 * @brief Removes leading and trailing whitespace characters from the input string.
 *
 * This function modifies the input string in place by trimming any whitespace
 * characters (such as spaces, tabs, or newlines) from both the beginning and end.
 *
 * @param s Pointer to the null-terminated string to be trimmed.
 * @return Pointer to the trimmed string (same as input pointer).
 */
static char *trim(char *s) {
    char *end;
    // Trim leading spaces, tabs, carriage returns and newlines
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    if (!*s) return s;
    // Trim trailing spaces, tabs, carriage returns and newlines
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = 0;
    return s;
}

/**
 * @brief Reads and processes the configuration file.
 *
 * This function opens the specified configuration file and parses its contents
 * to initialize or update the application's configuration settings.
 *
 * @param filename The path to the configuration file to be read.
 */
static void read_config_file(char *filename)
{
    // Read configuration from the file
    char line[256];
    FILE *config_file = fopen(filename, "r");
    if (config_file == NULL) {
        perror("Can't open config file");
        exit(EXIT_FAILURE);
    }
    int listen_port_set = 0;
    int forward_host_port_set = 0;
    int xor_key_set = 0;
    int something_set = 0;

    while (fgets(line, sizeof(line), config_file)) {
        // Remove trailing newlines, carriage returns, spaces and tabs
        while (strlen(line) && (line[strlen(line) - 1] == '\n' || line[strlen(line) - 1] == '\r' 
            || line[strlen(line) - 1] == ' ' || line[strlen(line) - 1] == '\t')) {
            line[strlen(line) - 1] = 0;
        }
        // Remove leading spaces and tabs
        while (strlen(line) && (line[0] == ' ' || line[0] == '\t')) {
            memmove(line, line + 1, strlen(line));
        }
        // Ignore comments
        char *comment_index = strstr(line, "#");
        if (comment_index != NULL) {
            *comment_index = 0;
        }
        // Skip empty lines or with spaces only
        if (strspn(line, " \t\r\n") == strlen(line)) {
            continue;
        }

        // It can be new section
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            if (something_set) {
                // new config, need to fork the process
                if (fork() == 0) {
                    return;
                }
            }
            size_t len = strlen(line) - 2;
            if (len > sizeof(section_name) - 1) {
                len = sizeof(section_name) - 1;
            }
            strncpy(section_name, line + 1, len);

            // Reset all the parameters
            listen_port = -1;
            memset(forward_host_port, 0, sizeof(forward_host_port));
            memset(xor_key, 0, sizeof(xor_key));
            memset(client_interface, 0, sizeof(client_interface));
            memset(static_bindings, 0, sizeof(static_bindings));
            memset(verbose_str, 0, sizeof(verbose_str));
            verbose = 2;
            listen_port_set = 0;
            forward_host_port_set = 0;
            xor_key_set = 0;
            something_set = 0;
            continue;
        }

        // Parse key-value pairs
        char *key = strtok(line, "=");
        key = trim(key);
        while (strlen(key) && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t' || key[strlen(key) - 1] == '\r' || key[strlen(key) - 1] == '\n')) {
            key[strlen(key) - 1] = 0;
        }
        char *value = strtok(NULL, "=");
        if (value == NULL) {
            log(LL_ERROR, "Invalid configuration line: %s", line);
            exit(EXIT_FAILURE);
        }
        value = trim(value);
        if (!*value) {
            log(LL_ERROR, "Invalid configuration line: %s", line);
            exit(EXIT_FAILURE);
        }

        if (strcmp(key, "source-lport") == 0) {
            listen_port = atoi(value);
            listen_port_set = 1;
            something_set = 1;
        } else if (strcmp(key, "target") == 0) {
            strncpy(forward_host_port, value, sizeof(forward_host_port) - 1);
            forward_host_port_set = 1;
            something_set = 1;
        } else if (strcmp(key, "key") == 0) {
            strncpy(xor_key, value, sizeof(xor_key) - 1);
            xor_key_set = 1;
            something_set = 1;
        } else if (strcmp(key, "source-if") == 0) {
            strncpy(client_interface, value, sizeof(client_interface) - 1);
            something_set = 1;
        }
         else if (strcmp(key, "target-if") == 0) {
            //strncpy(forward_interface, value, sizeof(forward_interface) - 1);
            log(LL_WARN, "The 'target-if' option is deprecated and will be ignored.");
            something_set = 1;
        } else if (strcmp(key, "source") == 0) {
            //strncpy(client_fixed_addr_port, value, sizeof(client_fixed_addr_port) - 1);
            log(LL_WARN, "The 'source' option is deprecated and will be ignored.");
            something_set = 1;
        } else if (strcmp(key, "target-lport") == 0) {
            //server_local_port = atoi(value);
            log(LL_WARN, "The 'target-lport' option is deprecated and will be ignored.");
            something_set = 1;
        }
        else if (strcmp(key, "static-bindings") == 0) {
            strncpy(static_bindings, value, sizeof(static_bindings) - 1);
            something_set = 1;
        }
        else if (strcmp(key, "verbose") == 0) {
            strncpy(verbose_str, value, sizeof(verbose_str) - 1);
            something_set = 1;
        } else {
            log(LL_ERROR, "Unknown configuration key: %s", key);
            exit(EXIT_FAILURE);
        }
    }
    fclose(config_file);
    if (!listen_port_set) {
        log(LL_ERROR, "'source-lport' is not set in the configuration file");
        exit(EXIT_FAILURE);
    }
    if (!forward_host_port_set) {
        log(LL_ERROR, "'target' is not set in the configuration file");
        exit(EXIT_FAILURE);
    }
    if (!xor_key_set) {
        log(LL_ERROR, "'key' is not set in the configuration file");
        exit(EXIT_FAILURE);
    }    
}

/* Parse a single option. */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
    switch (key)
    {
        case 'c':
            read_config_file(arg);
            break;
        case 'i':
            strncpy(client_interface, arg, sizeof(client_interface) - 1);
            break;
        case 's':
            log(LL_WARN, "The 'source' option is deprecated and will be ignored.");
            break;
        case 'p':
            listen_port = atoi(arg);
            break;
        case 'o':
            log(LL_WARN, "The 'target-if' option is deprecated and will be ignored.");
            break;
        case 't':
            strncpy(forward_host_port, arg, sizeof(forward_host_port) - 1);
            break;
        case 'r':
            log(LL_WARN, "The 'target-lport' option is deprecated and will be ignored.");
            break;
        case 'b':
            strncpy(static_bindings, arg, sizeof(static_bindings) - 1);
            break;
        case 'k':
            strncpy(xor_key, arg, sizeof(xor_key));
            break;
        case 'v':
            strncpy(verbose_str, arg, sizeof(verbose_str) - 1);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/* The options we understand. */
static const struct argp_option options[] = {
    { "config", 'c', "<config_file>", 0, "read configuration from file (can be used instead of the rest arguments)", .group = 0 },
    { "source-if", 'i', "<ip>", 0, "source interface to listen on (optional, default - 0.0.0.0, e.g. all)", .group = 1 },
    { "source", 's', "<ip>:<port>", OPTION_HIDDEN, "source client address and port (optional, default - auto, dynamic)", .group = 2 },
    { "source-lport", 'p', "<port>", 0, "source port to listen", .group = 3 },
    { "target-if", 'o', "<ip>", OPTION_HIDDEN, "target interface to use (optional, default - 0.0.0.0, e.g. all)", .group = 4 },
    { "target", 't', "<ip>:<port>", 0, "target IP and port", .group = 5 },
    { "target-lport", 'r', "<port>", OPTION_HIDDEN, "target port to listen (optional, default - random)", .group = 6 },
    { "key", 'k', "<key>", 0, "key to XOR the data", .group = 7 },
    { "static-bindings", 'b', "<ip>:<port>:<port>,...", 0, "comma-separated static bindings for two-way mode as <client_ip>:<client_port>:<forward_port>", .group = 8 },
    { "verbose", 'v', "<0-4>", 0, "verbosity level (optional, default - 2)", .group = 9 },
    { " ", 0, 0, OPTION_DOC , "0 - ERRORS (critical errors only)", .group = 9 },
    { " ", 0, 0, OPTION_DOC , "1 - WARNINGS (important messages: startup and shutdown messages)", .group = 9 },
    { " ", 0, 0, OPTION_DOC , "2 - INFO (informational messages: status messages, connection established, etc.)", .group = 9 },
    { " ", 0, 0, OPTION_DOC , "3 - DEBUG (detailed debug messages)", .group = 9 },
    { " ", 0, 0, OPTION_DOC , "4 - TRACE (very detailed debug messages, including packet dumps)", .group = 9 },
    { 0 }
};

/* Our argp parser. */
static struct argp argp = {
    .options = options,
    .parser = parse_opt,
    .args_doc = NULL,
#ifdef COMMIT
    .doc = "WireGuard Obfuscator\n(commit " COMMIT " @ " WG_OBFUSCATOR_GIT_REPO ")"
#else
	.doc = "WireGuard Obfuscator v" WG_OBFUSCATOR_VERSION
#endif
};

/**
 * Checks if the given data is obfuscated.
 *
 * @param data Pointer to the data buffer to check.
 * @return uint8_t Returns a non-zero value if the data is obfuscated, 0 otherwise.
 */
static inline uint8_t is_obfuscated(uint8_t *data) {
    return !(*((uint32_t*)data) >= 1 && *((uint32_t*)data) <= 4);
}

/**
 * @brief XORs the data in the given buffer with the provided key.
 *
 * This function applies a repeating XOR operation to each byte in the buffer
 * using the specified key. The key is repeated as necessary to match the length
 * of the buffer.
 *
 * @param buffer Pointer to the data buffer to be XORed.
 * @param length Length of the data buffer in bytes.
 * @param key Pointer to the key used for XOR operation.
 * @param key_length Length of the key in bytes.
 */
static inline void xor_data(uint8_t *buffer, int length, char *key, int key_length) {
    // Calculate the CRC8 based on the key
    uint8_t crc = 0, j;
    int i;
    for (i = 0; i < length; i++) 
    {
        // Get key byte and add the data length and the key length
        uint8_t inbyte = key[i % key_length] + length + key_length;
        for (j=0; j<8; j++) 
        {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
        // XOR the data with the CRC
        buffer[i] ^= crc;
    }
}

/**
 * @brief Encodes the given buffer using the specified key and version.
 *
 * This function applies an encoding algorithm to the input buffer using the provided key and version.
 *
 * @param buffer      Pointer to the data buffer to encode.
 * @param length      Length of the data buffer in bytes.
 * @param key         Pointer to the key used for encoding.
 * @param key_length  Length of the key in bytes.
 * @param version     Encoding version to use.
 * @return            0 on success, or a negative value on error.
 */
static inline int encode(uint8_t *buffer, int length, char *key, int key_length, uint8_t version) {
    if (version >= 1) {
        uint32_t packet_type = *((uint32_t*)buffer);
        // Add some randomness to the packet
        uint8_t rnd = 1 + (rand() % 255);
        buffer[0] ^= rnd; // Xor the first byte to a random value
        buffer[1] = rnd; // Set the second byte to a random value
        // Add dummy data to the packet
        if (length < MAX_DUMMY_LENGTH_TOTAL) {
            uint16_t dummy_length = 0;
            switch (packet_type) {
                case WG_TYPE_HANDSHAKE:
                case WG_TYPE_HANDSHAKE_RESP:
                    // length to MAX_DUMMY_LENGTH_HANDSHAKE
                    dummy_length = rand() % MAX_DUMMY_LENGTH_HANDSHAKE;
                    break;
                case WG_TYPE_COOKIE:
                case WG_TYPE_DATA:
                    // length to MAX_DUMMY_LENGTH_HANDSHAKE
#if MAX_DUMMY_LENGTH_DATA > 0
                    dummy_length = rand() % MAX_DUMMY_LENGTH_DATA;
#endif
                    break;
                default:
                    //assert(0);
                    break;
            }
            *((uint16_t*)(buffer+2)) = dummy_length; // Set the dummy length in the packet
            if (length + dummy_length > MAX_DUMMY_LENGTH_TOTAL) {
                dummy_length = MAX_DUMMY_LENGTH_TOTAL - length;
            }
            if (dummy_length > 0) {
                int i = length;
                length += dummy_length;
                for (; i < length; ++i) {
                    buffer[i] = 0xFF; // Fill with FFs, random data is not needed
                }
            }
        }
    }

    xor_data(buffer, length, key, key_length);

    return length;
}

static inline int decode(uint8_t *buffer, int length, char *key, int key_length, uint8_t *version_out) {
    xor_data(buffer, length, key, key_length);

    if (!is_obfuscated(buffer)) {
        // Looks like an old version
        *version_out = 0;
        return length;
    }

    buffer[0] ^= buffer[1]; // Restore the first byte by XORing it with the second byte
    buffer[1] = 0; // Set the second byte to 0
    length -= *((uint16_t*)(buffer+2)); // Remove dummy data length from the packet
    *((uint16_t*)(buffer+2)) = 0; // Reset the dummy length field to 0
    return length;
}

/**
 * @brief Handles incoming signals for the application.
 *
 * This function is registered as a signal handler and is invoked when the process
 * receives a signal. The specific actions taken depend on the signal received.
 *
 * @param signal The signal number received by the process.
 */
static void signal_handler(int signal) {
    client_entry_t *current_entry, *tmp;

    switch (signal) {
        case -1:
        case SIGINT:
        case SIGTERM:
            // Close all connections and clean up
            if (listen_sock) {
                close(listen_sock);
            }
            HASH_ITER(hh, conn_table, current_entry, tmp) {
                if (current_entry->server_sock) {
                    close(current_entry->server_sock);
                }
                HASH_DEL(conn_table, current_entry);
                free(current_entry);
            }
#ifdef USE_EPOLL
            if (epfd) {
                close(epfd);
            }
#endif
            log(LL_WARN, "Stopped.");
            break;
    }
    exit(signal != -1 ? EXIT_SUCCESS : EXIT_FAILURE);
}
#define FAILURE() signal_handler(-1)

/**
 * @brief Creates a new client_entry_t structure and initializes it with the provided client and forward addresses.
 *
 * @param client_addr Pointer to a struct sockaddr_in representing the client's address.
 * @param forward_addr Pointer to a struct sockaddr_in representing the address to which traffic should be forwarded.
 * @return Pointer to the newly created client_entry_t structure, or NULL on failure.
 */
static client_entry_t * new_client_entry(struct sockaddr_in *client_addr, struct sockaddr_in *forward_addr) {
    if (HASH_COUNT(conn_table) >= MAX_CLIENTS) {
        log(LL_ERROR, "Maximum number of clients reached (%d), cannot add new client", MAX_CLIENTS);
        return NULL;
    }
    client_entry_t * client_entry = malloc(sizeof(client_entry_t));
    if (!client_entry) {
        log(LL_ERROR, "Failed to allocate memory for client entry");
        return NULL;
    }
    memset(client_entry, 0, sizeof(client_entry_t));
    memcpy(&client_entry->client_addr, client_addr, sizeof(client_entry->client_addr));
    client_entry->server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_entry->server_sock < 0) {
        serror("Failed to create server socket for client");
        free(client_entry);
        return NULL;
    }
    // Set the server address to the specified one
    connect(client_entry->server_sock, (struct sockaddr *)forward_addr, sizeof(*forward_addr));
    // Get the assigned port number    
    socklen_t our_addr_len = sizeof(client_entry->our_addr);
    if (getsockname(client_entry->server_sock, (struct sockaddr *)&client_entry->our_addr, &our_addr_len) == -1) {
        serror("Failed to get socket port number");
        FAILURE();
    }

#ifdef USE_EPOLL    
    struct epoll_event e = {
        .events = EPOLLIN,
        .data.ptr = client_entry
    };
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_entry->server_sock, &e) != 0) {
        serror("epoll_ctl for client socket");
        close(client_entry->server_sock);
        free(client_entry);
        return NULL;
    }
#endif

    HASH_ADD(hh, conn_table, client_addr, sizeof(*client_addr), client_entry);

    log(LL_DEBUG, "Added binding: %s:%d:%d", 
        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
        ntohs(client_entry->our_addr.sin_port));

    return client_entry;
}

/**
 * @brief Creates a new static client entry.
 *
 * This function allocates and initializes a new client_entry_t structure
 * using the provided client and forward addresses, as well as the specified local port.
 *
 * @param client_addr Pointer to a sockaddr_in structure representing the client's address.
 * @param forward_addr Pointer to a sockaddr_in structure representing the address to forward to.
 * @param local_port The local port number to connect to the server.
 * @return Pointer to the newly created client_entry_t structure, or NULL on failure.
 */
static client_entry_t * new_client_entry_static(struct sockaddr_in *client_addr, struct sockaddr_in *forward_addr, uint16_t local_port) {
    if (HASH_COUNT(conn_table) >= MAX_CLIENTS) {
        log(LL_ERROR, "Maximum number of clients reached (%d), cannot add new client", MAX_CLIENTS);
        return NULL;
    }

    // Check if such client already exists
    client_entry_t *existing_entry;
    HASH_FIND(hh, conn_table, client_addr, sizeof(*client_addr), existing_entry);
    if (existing_entry) {
        log(LL_ERROR, "Binding with client %s:%d already exists", 
            inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
        return NULL;
    }

    client_entry_t * client_entry = malloc(sizeof(client_entry_t));
    if (!client_entry) {
        log(LL_ERROR, "Failed to allocate memory for client entry");
        return NULL;
    }
    memset(client_entry, 0, sizeof(client_entry_t));
    memcpy(&client_entry->client_addr, client_addr, sizeof(client_entry->client_addr));
    client_entry->server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_entry->server_sock < 0) {
        serror("Failed to create server socket for client");
        free(client_entry);
        return NULL;
    }
    // Bind the socket to the specified local port
    client_entry->our_addr.sin_family = AF_INET;
    // TODO: ability to bind to a specific address
    client_entry->our_addr.sin_addr.s_addr = INADDR_ANY;
    client_entry->our_addr.sin_port = htons(local_port);
    // Set the local port number
    if (bind(client_entry->server_sock, (struct sockaddr *)&client_entry->our_addr, sizeof(client_entry->our_addr)) < 0) {
        serror("Failed to bind server socket to %s:%d", 
            inet_ntoa(client_entry->our_addr.sin_addr), local_port);
        close(client_entry->server_sock);
        free(client_entry);
        return NULL;
    }
    // Set the server address to the specified one
    connect(client_entry->server_sock, (struct sockaddr *)forward_addr, sizeof(*forward_addr));

#ifdef USE_EPOLL    
    struct epoll_event e = {
        .events = EPOLLIN,
        .data.ptr = client_entry
    };
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_entry->server_sock, &e) != 0) {
        serror("epoll_ctl for client socket");
        close(client_entry->server_sock);
        free(client_entry);
        return NULL;
    }
#endif

    client_entry->is_static = 1;

    HASH_ADD(hh, conn_table, client_addr, sizeof(*client_addr), client_entry);

    // log(LL_DEBUG, "Added static binding: %s:%d:%d", 
    //     inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
    //     ntohs(client_entry->our_addr.sin_port));

    return client_entry;
}

#ifndef USE_EPOLL
static client_entry_t *find_by_server_sock(int fd) {
    client_entry_t *e, *tmp;
    HASH_ITER(hh, conn_table, e, tmp) {
        if (e->server_sock == fd) return e;
    }
    return NULL;
}
#endif

int main(int argc, char *argv[]) {
    struct sockaddr_in 
        listen_addr, // Address for listening socket, for receiving data from the client
        forward_addr; // Address for forwarding socket, for sending data to the server
    uint8_t buffer[BUFFER_SIZE];
    char target_host[256] = {0};
    int target_port = -1;
    int key_length = 0;
    unsigned long s_listen_addr_client = INADDR_ANY;
    long now, last_cleanup_time = 0;

#ifdef USE_EPOLL
    struct epoll_event events[MAX_EVENTS];
#else
    struct pollfd pollfds[MAX_CLIENTS + 1];
#endif

    /* Parse command line arguments */
    if (argc == 1) {
        fprintf(stderr, "No arguments provided, use \"%s --help\" command for usage information", argv[0]);
        exit(EXIT_FAILURE);
    }
    if (argp_parse(&argp, argc, argv, 0, 0, 0) != 0) {
        fprintf(stderr, "Failed to parse command line arguments");
        exit(EXIT_FAILURE);
    }
  
    /* Check the parameters */

    // Check the listening port
    if (listen_port < 0) {
        log(LL_ERROR, "'source-lport' is not set");
        exit(EXIT_FAILURE);
    }
 
    // Check the target host and port
    if (!forward_host_port[0]) {
        log(LL_ERROR, "'target' is not set");
        exit(EXIT_FAILURE);
    } else {
        char *port_delimiter = strchr(forward_host_port, ':');
        if (port_delimiter == NULL) {
            log(LL_ERROR, "Invalid target host:port format: %s", forward_host_port);
            exit(EXIT_FAILURE);
        }
        *port_delimiter = 0;
        strncpy(target_host, forward_host_port, sizeof(target_host) - 1);
        target_port = atoi(port_delimiter + 1);
        if (target_port <= 0) {
            log(LL_ERROR, "Invalid target port: %s", port_delimiter + 1);
            exit(EXIT_FAILURE);
        }
    }

    // Check the key
    key_length = strlen(xor_key);
    if (key_length == 0) {
        log(LL_ERROR, "Key is not set");
        exit(EXIT_FAILURE);
    }

    // Check the client interface
    if (client_interface[0]) {
        s_listen_addr_client = inet_addr(client_interface);
        if (s_listen_addr_client == INADDR_NONE) {
            struct hostent *he = gethostbyname(client_interface);
            if (he == NULL) {
                log(LL_ERROR, "Invalid source interface: %s", client_interface);
                exit(EXIT_FAILURE);
            }
            s_listen_addr_client = *(unsigned long *)he->h_addr;
        }
    }

    // Check and set the verbosity level
    if (verbose_str[0]) {
        verbose = atoi(verbose_str);
        if (verbose < 0 || verbose > 4) {
            log(LL_ERROR, "Invalid verbosity level: %s (must be between 0 and 4)", verbose_str);
            exit(EXIT_FAILURE);
        }
    }

    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Create listening socket */
    if ((listen_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        serror("Can't create source socket to listen");
        exit(EXIT_FAILURE);
    }

    /* Bind the listening socket to the specified address and port */
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = s_listen_addr_client;
    listen_addr.sin_port = htons(listen_port);
    if (bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        serror("Failed to bind source socket to %s:%d", 
            inet_ntoa(listen_addr.sin_addr), ntohs(listen_addr.sin_port));
        FAILURE();
    }
    log(LL_WARN, "Listening on port %s:%d for source", inet_ntoa(listen_addr.sin_addr), ntohs(listen_addr.sin_port));

    /* Use epoll for events if enabled */
#ifdef USE_EPOLL
    epfd = epoll_create1(0);
    if (epfd < 0) {
        serror("epoll_create1");
        FAILURE();
    }
    {
        struct epoll_event ev = {
            .events = EPOLLIN,
            .data.fd = listen_sock
        };
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &ev) != 0) {
            serror("epoll_ctl for listen_sock");
            FAILURE();
        }
    }
#endif

    /* Set up forward address */
    memset(&forward_addr, 0, sizeof(forward_addr));
    forward_addr.sin_family = AF_INET;
    struct hostent *host = gethostbyname(target_host);
    if (host == NULL) {
        log(LL_ERROR, "Can't resolve hostname: %s", target_host);
        FAILURE();
    }
    log(LL_DEBUG, "Resolved target hostname '%s' to %s", target_host, inet_ntoa(*(struct in_addr *)host->h_addr));
    forward_addr.sin_addr = *(struct in_addr *)host->h_addr;
    if (target_port <= 0 || target_port > 65535) {
        log(LL_ERROR, "Invalid target port: %d", target_port);
        FAILURE();
    }
    forward_addr.sin_port = htons(target_port);
    log(LL_WARN, "Target: %s:%d", target_host, target_port);

    /* Add static bindings if provided */
    if (static_bindings[0]) {
        // Parse static bindings
        char *binding = strtok(static_bindings, ",");
        while (binding) {
            // Trim leading and trailing spaces
            binding = trim(binding);
            char *colon1 = strchr(binding, ':');
            if (!colon1) {
                log(LL_ERROR, "Invalid static binding format: %s", binding);
                exit(EXIT_FAILURE);
            }
            char *colon2 = strchr(colon1 + 1, ':');
            if (!colon2) {
                log(LL_ERROR, "Invalid static binding format: %s", binding);
                exit(EXIT_FAILURE);
            }
            *colon1 = 0;
            *colon2 = 0;

            struct sockaddr_in client_addr = {0};
            client_addr.sin_family = AF_INET;
            struct hostent *host = gethostbyname(binding);
            if (host == NULL) {
                log(LL_ERROR, "Can't resolve hostname '%s' for static binding '%s:%s:%s'", 
                    binding, binding, colon1 + 1, colon2 + 1);
                FAILURE();
            }
            log(LL_DEBUG, "Resolved static binding hostname '%s' to %s", binding, inet_ntoa(*(struct in_addr *)host->h_addr));
            client_addr.sin_addr = *(struct in_addr *)host->h_addr;
            int remote_port = atoi(colon1 + 1);
            if (remote_port <= 0 || remote_port > 65535) {
                log(LL_ERROR, "Invalid port '%s' for static binding '%s:%s:%s'",
                    colon1 + 1, binding, colon1 + 1, colon2 + 1);
                FAILURE();
            }
            int local_port = atoi(colon2 + 1);
            if (local_port <= 0 || local_port > 65535) {
                log(LL_ERROR, "Invalid port '%s' for static binding '%s:%s:%s'",
                    colon2 + 1, binding, colon1 + 1, colon2 + 1);
                FAILURE();
            }
            client_addr.sin_port = htons(remote_port);

            if (!new_client_entry_static(&client_addr, &forward_addr, local_port)) {
                log(LL_ERROR, "Failed to create static binding: %s:%s:%s",
                    binding, colon1 + 1, colon2 + 1);
                FAILURE();
            }

            log(LL_WARN, "Added static binding: %s:%d <-> %d:obfuscator:%d <-> %s:%d", 
                binding, remote_port, listen_port,
                local_port, target_host, target_port);

            binding = strtok(NULL, ",");
        }
    }

    log(LL_WARN, "WireGuard obfuscator successfully started");

    /* Main loop */
    while (1) {
        // Using epoll or poll to wait for events
#ifdef USE_EPOLL
        int events_n = epoll_wait(epfd, events, MAX_EVENTS, POLL_TIMEOUT);
        if (events_n < 0) {
            serror("epoll_wait");
            FAILURE();
        }
#else
        int nfds = 0;
        pollfds[nfds].fd = listen_sock;
        pollfds[nfds].events = POLLIN;
        nfds++;
        client_entry_t *entry, *tmp;
        HASH_ITER(hh, conn_table, entry, tmp) {
            if (nfds >= MAX_CLIENTS) {
                log(LL_DEBUG, "Too many clients, cannot add more");
                break;
            }
            pollfds[nfds].fd = entry->server_sock;
            pollfds[nfds].events = POLLIN;
            nfds++;
        }
        int ret = poll(pollfds, nfds, POLL_TIMEOUT);
        if (ret < 0) {
            serror("poll");
            FAILURE();
        }
#endif

        // Get the current time
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        now = now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000;

#ifdef USE_EPOLL
        for (int e = 0; e < events_n; e++) {
            struct epoll_event *event = &events[e];
            if (event->data.fd == listen_sock) {
#else
        for (int e = 0; e < nfds; e++) if (pollfds[e].revents & POLLIN) {
            if (pollfds[e].fd == listen_sock) {
#endif
                /* *** Handle incoming data from the clients *** */
                struct sockaddr_in sender_addr = {0};
                socklen_t sender_addr_len = sizeof(sender_addr);
                int length = recvfrom(listen_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&sender_addr, &sender_addr_len);
                if (length < 0) {
                    serror("recvfrom");
                    continue;
                }
                if (length < 4) {
                    log(LL_DEBUG, "Received too short packet from %s:%d (%d bytes), ignoring", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length);
                    continue;
                }

                client_entry_t *client_entry;
                HASH_FIND(hh, conn_table, &sender_addr, sizeof(sender_addr), client_entry);

                uint8_t obfuscated = is_obfuscated(buffer);
                uint8_t version = OBFUSCATION_VERSION;

                if (verbose >= LL_TRACE) {
                    log(LL_TRACE, "Received %d bytes from %s:%d to %s:%d (known=%s, obfuscated=%s)",
                        length,
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        target_host, target_port,
                        client_entry ? "yes" : "no", obfuscated ? "yes" : "no");
                    if (obfuscated) {
                        trace("X->: ");
                    } else {
                        trace("O->: ");
                    }
                    for (int i = 0; i < length; ++i) {
                        trace("%02X ", buffer[i]);
                    }
                    trace("\n");
                }

                if (obfuscated) {
                    // decode
                    length = decode(buffer, length, xor_key, key_length, &version);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to decode packet from %s:%d (too short, length=%d)",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length);
                        continue;
                    }
                }

                // Is it handshake?
                if (*((uint32_t*)buffer) == WG_TYPE_HANDSHAKE) {
                    log(LL_DEBUG, "Received WireGuard handshake from %s:%d to %s:%d (%d bytes, obfuscated=%s)",
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        target_host, target_port,
                        length, 
                        obfuscated ? "yes" : "no");

                    if (!client_entry) {
                        client_entry = new_client_entry(&sender_addr, &forward_addr);
                        if (!client_entry) {
                            continue;
                        }
                        client_entry->version = version;
                        if (version < OBFUSCATION_VERSION) {
                            log(LL_WARN, "Client %s:%d uses old obfuscation version, downgrading from %d to %d", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), 
                                OBFUSCATION_VERSION, version);
                        }
                    }

                    client_entry->handshake_direction = HANDSHAKE_DIRECTION_CLIENT_TO_SERVER;
                    client_entry->last_handshake_request_time = now;
                }
                // Is it handshake response?
                else if (*((uint32_t*)buffer) == WG_TYPE_HANDSHAKE_RESP) {
                    if (!client_entry) {
                        log(LL_DEBUG, "Received WireGuard handshake response from %s:%d, but no connection entry found for this client",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port));
                        continue;
                    }

                    log(LL_DEBUG, "Received WireGuard handshake response from %s:%d to %s:%d (%d bytes, obfuscated=%s)",
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        target_host, target_port,
                        length, obfuscated ? "yes" : "no");

                    // Check handshake timeout
                    if (now - client_entry->last_handshake_request_time > HANDSHAKE_TIMEOUT) {
                        log(LL_DEBUG, "Ignoring WireGuard handshake response, handshake timeout");
                        continue;
                    }

                    if (client_entry->handshake_direction != HANDSHAKE_DIRECTION_SERVER_TO_CLIENT) {
                        log(LL_DEBUG, "Received handshake response from %s:%d to %s:%d, but the handshake direction is not set to server-to-client",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                            target_host, target_port);
                        continue;;
                    }

                    log(!client_entry->handshaked ? LL_INFO : LL_DEBUG, "Handshake established with %s:%d to %s:%d (reverse)",
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        target_host, target_port);
                    client_entry->handshaked = 1;
                    client_entry->last_handshake_time = now;
                }
                // If it's not a handshake or handshake response, connection is not established yet
                else if (!client_entry || !client_entry->handshaked) {
                    log(LL_DEBUG, "Ignoring data from %s:%d to %s:%d until the handshake is completed",
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        target_host, target_port);
                    continue;
                }

                if (!obfuscated) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, xor_key, key_length, client_entry->version);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d (too short, length=%d)",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length);
                        continue;
                    }
                }

                if (verbose >= LL_TRACE) {
                    if (!obfuscated) {
                        trace("X->: ");
                    } else {
                        trace("O->: ");
                    }
                    for (int i = 0; i < length; ++i) {
                        trace("%02X ", buffer[i]);
                    }
                    trace("\n");
                }

                sendto(client_entry->server_sock, buffer, length, 0, (struct sockaddr *)&forward_addr, sizeof(forward_addr));
                client_entry->last_activity_time = now;
            } else { // if (event->data.fd == listen_sock)
                /* *** Handle data from the server *** */
#ifdef USE_EPOLL
                client_entry_t *client_entry = event->data.ptr;
#else
                client_entry_t *client_entry = find_by_server_sock(pollfds[e].fd);
#endif
                int length = recv(client_entry->server_sock, buffer, BUFFER_SIZE, 0);
                if (length < 0) {
                    serror("recv");
                    continue;
                }
                if (length < 4) {
                    log(LL_DEBUG, "Received too short packet from %s:%d (%d bytes), ignoring", target_host, target_port, length);
                    continue;
                }

                uint8_t obfuscated = is_obfuscated(buffer);

                if (verbose >= LL_TRACE) {
                    log(LL_TRACE, "Received %d bytes from %s:%d to %s:%d (obfuscated=%s)",
                        length,
                        target_host, target_port, 
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        obfuscated ? "yes" : "no");
                    if (obfuscated) {
                        trace("<-X: ");
                    } else {
                        trace("<-O: ");
                    }
                    for (int i = 0; i < length; ++i) {
                        trace("%02X ", buffer[i]);
                    }
                    trace("\n");
                }

                if (obfuscated) {
                    // decode
                    length = decode(buffer, length, xor_key, key_length, &client_entry->version);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to decode packet from %s:%d", target_host, target_port);
                        continue;
                    }
                }

                // Is it handshake?
                if (*((uint32_t*)buffer) == WG_TYPE_HANDSHAKE) {
                    log(LL_DEBUG, "Received WireGuard handshake from %s:%d to %s:%d (%d bytes, obfuscated=%s)",
                        target_host, target_port,
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        length, 
                        obfuscated ? "yes" : "no");

                    client_entry->handshake_direction = HANDSHAKE_DIRECTION_SERVER_TO_CLIENT;
                    client_entry->last_handshake_request_time = now;
                }
                // Is it handshake response?
                else if (*((uint32_t*)buffer) == WG_TYPE_HANDSHAKE_RESP) {
                    log(LL_DEBUG, "Received WireGuard handshake response from %s:%d to %s:%d (%d bytes, obfuscated=%s)",
                        target_host, target_port,
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        length, obfuscated ? "yes" : "no");

                    // Check handshake timeout
                    if (now - client_entry->last_handshake_request_time > HANDSHAKE_TIMEOUT) {
                        log(LL_DEBUG, "Ignoring WireGuard handshake response, handshake timeout");
                        continue;
                    }

                    if (client_entry->handshake_direction != HANDSHAKE_DIRECTION_CLIENT_TO_SERVER) {
                        log(LL_DEBUG, "Received handshake response from %s:%d to %s:%d, but the handshake direction is not set to client-to-server",
                            target_host, target_port,
                            inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port));
                        continue;
                    }

                    log(!client_entry->handshaked ? LL_INFO : LL_DEBUG, "Handshake established with %s:%d to %s:%d (direct)",
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        target_host, target_port);
                    client_entry->handshaked = 1;
                    client_entry->last_handshake_time = now;
                }
                // If it's not a handshake or handshake response, connection is not established yet
                else if (!client_entry->handshaked) {
                    log(LL_DEBUG, "Ignoring response from %s:%d to %s:%d until the handshake is completed",
                        target_host, target_port,
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port));
                    continue;
                }

                if (!obfuscated) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, xor_key, key_length, client_entry->version);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d", target_host, target_port);
                        continue;
                    }
                }
                
                if (verbose >= LL_TRACE) {
                    if (!obfuscated) {
                        trace("<-X: ");
                    } else {
                        trace("<-O: ");
                    }
                    for (int i = 0; i < length; ++i) {
                        trace("%02X ", buffer[i]);
                    }
                    trace("\n");
                }

                // Send the response back to the original client
                sendto(listen_sock, buffer, length, 0, (struct sockaddr *)&client_entry->client_addr, sizeof(client_entry->client_addr));
                client_entry->last_activity_time = now;
            } // if (event->data.fd != listen_sock)
        } // for (int e = 0; e < events_n; e++)

        /* *** Cleanup old entries *** */
        if (now - last_cleanup_time >= CLEANUP_INTERVAL) {
            client_entry_t *current_entry, *tmp;
            // Iterate over all client entries
            HASH_ITER(hh, conn_table, current_entry, tmp) {
                // Check if the entry is idle for too long
                if (
                    (
                        (now - current_entry->last_activity_time >= IDLE_TIMEOUT)
                        || (!current_entry->handshaked && (now - current_entry->last_handshake_request_time >= HANDSHAKE_TIMEOUT))
                    ) && !current_entry->is_static // Do not remove static entries
                ) {
                    log(LL_INFO, "Removing idle client %s:%d", inet_ntoa(current_entry->client_addr.sin_addr), ntohs(current_entry->client_addr.sin_port));
#ifdef USE_EPOLL
                    epoll_ctl(epfd, EPOLL_CTL_DEL, current_entry->server_sock, NULL);
#endif
                    close(current_entry->server_sock);
                    HASH_DEL(conn_table, current_entry);
                    free(current_entry);
                }
            }
            // Update the last cleanup time
            last_cleanup_time = now;
        }
    } // while (1)

    // You should never reach this point, but just in case
    return 0;
}
