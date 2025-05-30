#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <argp.h>
#include <sys/epoll.h>
#include "wg-obfuscator.h"
#include "uthash.h"
#include "commit.h"

#define log(level, fmt, ...) { if (verbose >= level)            \
    fprintf(stderr, "[%s][%c] " fmt, section_name,              \
    (                                                           \
          level == LL_ERROR ? 'E'                               \
        : level == LL_WARN  ? 'W'                               \
        : level == LL_INFO  ? 'I'                               \
        : level == LL_DEBUG ? 'D'                               \
        : level == LL_TRACE ? 'T'                               \
        : '?'                                                   \
    ), ##__VA_ARGS__);                                          \
}
#define trace(fmt, ...) if (verbose >= LL_TRACE) fprintf(stderr, fmt, ##__VA_ARGS__)

static int listen_sock = 0, /*forward_sock = 0,*/ epfd = 0; // sockets and epoll file descriptor
static client_entry_t *conn_table = NULL; // hash table for client connections

// main parameters (TODO: IPv6?)
static char section_name[256] = "main";
static int listen_port = -1;
static char forward_host_port[256] = {0};
static char xor_key[256] = {0};
// optional parameters: client and forward interfaces
static char client_interface[256] = {0};
static char forward_interface[256] = {0};
// optional parameters: fixed client address
static char client_fixed_addr_port[256] = {0};
static int server_local_port = 0;
// verbosity level
static char verbose_str[256] = {0};
static int verbose = 2;

static void perror_sect(char *str, char* section)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "[%s][E] %s", section, str);
    perror(buf);
}

#define serror(x) perror_sect(x, section_name)

static void read_config_file(char *filename)
{
    // Read configuration from file
    char line[256];
    FILE *config_file = fopen(filename, "r");
    if (config_file == NULL) {
        serror("config file");
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
            memset(forward_interface, 0, sizeof(forward_interface));
            memset(client_fixed_addr_port, 0, sizeof(client_fixed_addr_port));
            server_local_port = 0;
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
        // Trim leading and trailing spaces
        while (strlen(key) && (key[0] == ' ' || key[0] == '\t' || key[0] == '\r' || key[0] == '\n')) {
            key++;
        }
        while (strlen(key) && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t' || key[strlen(key) - 1] == '\r' || key[strlen(key) - 1] == '\n')) {
            key[strlen(key) - 1] = 0;
        }
        char *value = strtok(NULL, "=");
        if (value == NULL) {
            log(LL_ERROR, "Invalid configuration line: %s\n", line);
            exit(EXIT_FAILURE);
        }
        // Trim leading and trailing spaces
        while (strlen(value) && (value[0] == ' ' || value[0] == '\t' || value[0] == '\r' || value[0] == '\n')) {
            value++;
        }
        while (strlen(value) && (value[strlen(value) - 1] == ' ' || value[strlen(value) - 1] == '\t' || value[strlen(value) - 1] == '\r' || value[strlen(value) - 1] == '\n')) {
            value[strlen(value) - 1] = 0;
        }
        if (value == NULL) {
            log(LL_ERROR, "Invalid configuration line: %s\n", line);
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
        } else if (strcmp(key, "target-if") == 0) {
            strncpy(forward_interface, value, sizeof(forward_interface) - 1);
            something_set = 1;
        } else if (strcmp(key, "source") == 0) {
            strncpy(client_fixed_addr_port, value, sizeof(client_fixed_addr_port) - 1);
            something_set = 1;
        } else if (strcmp(key, "target-lport") == 0) {
            server_local_port = atoi(value);
            something_set = 1;
        } else if (strcmp(key, "verbose") == 0) {
            strncpy(verbose_str, value, sizeof(verbose_str) - 1);
            something_set = 1;
        } else {
            log(LL_ERROR, "Unknown configuration key: '%s'\n", key);
            exit(EXIT_FAILURE);
        }
    }
    fclose(config_file);
    if (!listen_port_set) {
        log(LL_ERROR, "'source-lport' is not set in the configuration file\n");
        exit(EXIT_FAILURE);
    }
    if (!forward_host_port_set) {
        log(LL_ERROR, "'target' is not set in the configuration file\n");
        exit(EXIT_FAILURE);
    }
    if (!xor_key_set) {
        log(LL_ERROR, "'key' is not set in the configuration file\n");
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
            strncpy(client_fixed_addr_port, arg, sizeof(client_fixed_addr_port) - 1);
            break;
        case 'p':
            listen_port = atoi(arg);
            break;
        case 'o':
            strncpy(forward_interface, arg, sizeof(forward_interface) - 1);
            break;
        case 't':
            strncpy(forward_host_port, arg, sizeof(forward_host_port) - 1);
            break;
        case 'r':
            server_local_port = atoi(arg);
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
    { "source", 's', "<ip>:<port>", 0, "source client address and port (optional, default - auto, dynamic)", .group = 2 },
    { "source-lport", 'p', "<port>", 0, "source port to listen", .group = 3 },
    { "target-if", 'o', "<ip>", 0, "target interface to use (optional, default - 0.0.0.0, e.g. all)", .group = 4 },
    { "target", 't', "<ip>:<port>", 0, "target IP and port", .group = 5 },
    { "target-lport", 'r', "<port>", 0, "target port to listen (optional, default - random)", .group = 6 },
    { "key", 'k', "<key>", 0, "key to XOR the data", .group = 7 },
    { "verbose", 'v', "<0-4>", 0, "verbosity level (optional, default - 2)", .group = 8 },
    { " ", 0, 0, OPTION_DOC , "0 - silent, critical startup errors only", .group = 8 },
    { " ", 0, 0, OPTION_DOC , "1 - only startup/shutdown messages", .group = 8 },
    { " ", 0, 0, OPTION_DOC , "2 - status messages", .group = 8 },
    { " ", 0, 0, OPTION_DOC , "3 - warnings", .group = 8 },
    { " ", 0, 0, OPTION_DOC , "4 - debug mode, print out all transmitted data", .group = 8 },
    { 0 }
};

/* Our argp parser. */
static struct argp argp = {
    .options = options,
    .parser = parse_opt,
    .args_doc = NULL,
#ifdef COMMIT
    .doc = "WireGuard Obfuscator\n(commit " COMMIT " @ " GIT_REPO ")"
#else
	.doc = "WireGuard Obfuscator v" VERSION
#endif
};

static uint8_t is_obfuscated(uint8_t *data) {
    return !(*((uint32_t*)data) >= 1 && *((uint32_t*)data) <= 4);
}

// XOR the data with the key
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

// Handle signals
static void signal_handler(int signal) {
    client_entry_t *current_entry, *tmp;

    switch (signal) {
        case -1:
        case SIGINT:
        case SIGTERM:
            // Stop the main loop
            if (listen_sock) {
                close(listen_sock);
            }
            // if (forward_sock) {
            //     close(forward_sock);
            // }
            HASH_ITER(hh, conn_table, current_entry, tmp) {
                if (current_entry->server_sock) {
                    close(current_entry->server_sock);
                }
                HASH_DEL(conn_table, current_entry);
                free(current_entry);
            }
            if (epfd) {
                close(epfd);
            }
            log(LL_WARN, "Stopped.\n");
            break;
    }
    exit(signal != -1 ? EXIT_SUCCESS : EXIT_FAILURE);
}
#define FAILURE() signal_handler(-1)

static client_entry_t * new_client_entry(struct sockaddr_in *client_addr, struct sockaddr_in *forward_addr) {
    if (HASH_COUNT(conn_table) >= MAX_CLIENTS) {
        log(LL_ERROR, "Maximum number of clients reached (%d), cannot add new client\n", MAX_CLIENTS);
        return NULL;
    }
    client_entry_t * client_entry = malloc(sizeof(client_entry_t));
    if (!client_entry) {
        log(LL_ERROR, "Failed to allocate memory for client entry\n");
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
    connect(client_entry->server_sock, (struct sockaddr *)forward_addr, sizeof(*forward_addr));
    // Get the assigned port number
    socklen_t our_addr_len = sizeof(client_entry->our_addr);
    if (getsockname(client_entry->server_sock, (struct sockaddr *)&client_entry->our_addr, &our_addr_len) == -1) {
        serror("failed to get socket port number");
        FAILURE();
    }
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
    HASH_ADD(hh, conn_table, client_addr, sizeof(client_entry->client_addr), client_entry);

    return client_entry;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in 
        listen_addr, // Address for listening socket, for receiving data from the client
        forward_addr; // Address for forwarding socket, for sending data to the server
    uint8_t buffer[BUFFER_SIZE];
    char forward_host[256] = {0};
    int forward_port = -1;
    int key_length = 0;
    unsigned long s_listen_addr_client = INADDR_ANY;
    unsigned long s_listen_addr_forward = INADDR_ANY;
    struct epoll_event events[MAX_EVENTS];
    struct timespec now, last_cleanup_time;

    // Parse command line arguments
    if (argc == 1) {
        log(LL_ERROR, "No arguments provided, use \"%s --help\" command for usage information\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    if (argp_parse(&argp, argc, argv, 0, 0, 0) != 0) {
        log(LL_ERROR, "Failed to parse command line arguments\n");
        exit(EXIT_FAILURE);
    }
  
    // Check the parameters
    key_length = strlen(xor_key);
    if (listen_port < 0) {
        log(LL_ERROR, "'source-lport' is not set\n");
        exit(EXIT_FAILURE);
    }
    /*
    if (client_fixed_addr_port[0]) {
        char *port_delimiter = strchr(client_fixed_addr_port, ':');
        if (port_delimiter == NULL) {
            log(LL_ERROR, "Invalid source ip:port format: %s\n", client_fixed_addr_port);
            exit(EXIT_FAILURE);
        }
        *port_delimiter = 0;
        last_sender_addr.sin_addr.s_addr = inet_addr(client_fixed_addr_port);
        if (last_sender_addr.sin_addr.s_addr == INADDR_NONE) {
            log(LL_ERROR, "Invalid source ip: %s\n", client_fixed_addr_port);
            exit(EXIT_FAILURE);
        }
        last_sender_addr.sin_port = htons(atoi(port_delimiter + 1));
        if (last_sender_addr.sin_port <= 0) {
            log(LL_ERROR, "Invalid source port: %s\n", port_delimiter + 1);
            exit(EXIT_FAILURE);
        }
        last_sender_addr.sin_family = AF_INET;
        log(LL_INFO, "Source: %s:%d\n", inet_ntoa(last_sender_addr.sin_addr), ntohs(last_sender_addr.sin_port));
    } else {
        log(LL_INFO, "Source: any/auto\n");
    }
    */  
    if (!forward_host_port[0]) {
        log(LL_ERROR, "'target' is not set\n");
        exit(EXIT_FAILURE);
    } else {
        char *port_delimiter = strchr(forward_host_port, ':');
        if (port_delimiter == NULL) {
            log(LL_ERROR, "Invalid target host:port format: %s\n", forward_host_port);
            exit(EXIT_FAILURE);
        }
        *port_delimiter = 0;
        strncpy(forward_host, forward_host_port, sizeof(forward_host) - 1);
        forward_port = atoi(port_delimiter + 1);
        if (forward_port <= 0) {
            log(LL_ERROR, "Invalid target port: %s\n", port_delimiter + 1);
            exit(EXIT_FAILURE);
        }
        log(LL_INFO, "Target: %s:%d\n", forward_host, forward_port);
    } 
    if (key_length == 0) {
        log(LL_ERROR, "Key is not set\n");
        exit(EXIT_FAILURE);
    }
    if (client_interface[0]) {
        s_listen_addr_client = inet_addr(client_interface);
    }
    if (forward_interface[0]) {
        s_listen_addr_forward = inet_addr(forward_interface);
    }
    if (s_listen_addr_client == INADDR_NONE) {
        log(LL_ERROR, "Invalid source interface: %s\n", client_interface);
        exit(EXIT_FAILURE);
    }
    if (s_listen_addr_forward == INADDR_NONE) {
        log(LL_ERROR, "Invalid target interface: %s\n", forward_interface);
        exit(EXIT_FAILURE);
    }
    if (verbose_str[0]) {
        verbose = atoi(verbose_str);
        if (verbose < 0 || verbose > 4) {
            log(LL_ERROR, "Invalid verbosity level: %s\n", verbose_str);
            exit(EXIT_FAILURE);
        }
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create listening socket
    if ((listen_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        serror("source socket");
        exit(EXIT_FAILURE);
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = s_listen_addr_client;
    listen_addr.sin_port = htons(listen_port);

    if (bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        serror("source socket bind");
        FAILURE();
    }
    log(LL_WARN, "Listening on port %s:%d for source\n", inet_ntoa(listen_addr.sin_addr), ntohs(listen_addr.sin_port));

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

    // Create forwarding socket if fixed port is used
    // TODO
    /*
    if (server_local_port) {
        if ((forward_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            serror("target socket");
            FAILURE();
        }

        // Create a client address for the forward socket
        memset(&forward_client_addr, 0, sizeof(forward_client_addr));
        forward_client_addr.sin_family = AF_INET;
        forward_client_addr.sin_addr.s_addr = s_listen_addr_forward;
        forward_client_addr.sin_port = server_local_port ? htons(server_local_port) : 0;

        if (bind(forward_sock, (struct sockaddr *)&forward_client_addr, sizeof(forward_client_addr)) < 0) {
            serror("target socket bind");
            FAILURE();
        }

        // Get the assigned port number
        socklen_t forward_client_addr_len = sizeof(forward_client_addr);
        if (getsockname(forward_sock, (struct sockaddr *)&forward_client_addr, &forward_client_addr_len) == -1) {
            serror("failed to get socket port number");
            FAILURE();
        }
        log(LL_WARN, "Listening on port %s:%d for target\n", inet_ntoa(forward_client_addr.sin_addr), ntohs(forward_client_addr.sin_port));

        struct epoll_event ev = {
            .events = EPOLLIN,
            .data.fd = listen_sock
        };
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, forward_sock, &ev) != 0) {
            serror("epoll_ctl for forward_sock");
            FAILURE();
        }
    }
    */

    // Set up forward address
    memset(&forward_addr, 0, sizeof(forward_addr));
    forward_addr.sin_family = AF_INET;
    struct hostent *host = gethostbyname(forward_host);
    if (host == NULL) {
        serror("can't resolve hostname");
        FAILURE();
    }
    forward_addr.sin_addr = *(struct in_addr *)host->h_addr;
    forward_addr.sin_port = htons(forward_port);

    log(LL_WARN, "WireGuard obfuscator successfully started\n");

    clock_gettime(CLOCK_MONOTONIC, &last_cleanup_time);

    while (1) {
        int events_n = epoll_wait(epfd, events, MAX_EVENTS, EPOLL_TIMEOUT);
        if (events_n < 0) {
            serror("epoll_wait");
            FAILURE();
        }

        clock_gettime(CLOCK_MONOTONIC, &now);

        for (int e = 0; e < events_n; e++) {
            struct epoll_event *event = &events[e];

            if (event->data.fd == listen_sock) {
                struct sockaddr_in sender_addr;
                socklen_t sender_addr_len = sizeof(sender_addr);
                int length = recvfrom(listen_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&sender_addr, &sender_addr_len);
                if (length < 0) {
                    serror("recvfrom");
                    continue;
                }
                if (length < 4) {
                    log(LL_DEBUG, "Received too short packet from %s:%d (%d bytes), ignoring\n", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length);
                    continue;
                }

                client_entry_t *client_entry;
                HASH_FIND(hh, conn_table, &sender_addr, sizeof(sender_addr), client_entry);

                uint8_t obfuscated = is_obfuscated(buffer);
                uint8_t version = OBFUSCATION_VERSION;

                if (verbose >= LL_TRACE) {
                    log(LL_TRACE, "Received %d bytes from %s:%d to %s:%d (known=%s, obfuscated=%s)\n",
                        length,
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        forward_host, forward_port,
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
                        log(LL_ERROR, "Failed to decode packet from %s:%d\n", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port));
                        continue;
                    }
                }

                // Is it handshake?
                if (*((uint32_t*)buffer) == WG_TYPE_HANDSHAKE) {
                    log(LL_INFO, "Received WireGuard handshake from %s:%d (%d bytes, obfuscated=%s)\n", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length, 
                        obfuscated ? "yes" : "no");

                    if (!client_entry) {
                        client_entry = new_client_entry(&sender_addr, &forward_addr);
                        if (!client_entry) {
                            continue;
                        }
                        client_entry->version = version;
                        if (version < OBFUSCATION_VERSION) {
                            log(LL_WARN, "Client %s:%d uses old obfuscation version, downgrading from %d to %d\n", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), 
                                OBFUSCATION_VERSION, version);
                        }
                    }

                    client_entry->last_handshake_request_time = now;
                } else if (!client_entry || !client_entry->handshaked) {
                    log(LL_DEBUG, "Ignoring data from %s:%d to %s:%d until the handshake is completed\n",
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        forward_host, forward_port);
                    continue;
                }

                if (!obfuscated) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, xor_key, key_length, client_entry->version);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d\n", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port));
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
                client_entry_t *client_entry = event->data.ptr;
                int length = recv(client_entry->server_sock, buffer, BUFFER_SIZE, 0);
                if (length < 0) {
                    serror("recv");
                    continue;
                }
                if (length < 4) {
                    log(LL_DEBUG, "Received too short packet from %s:%d (%d bytes), ignoring\n", forward_host, forward_port, length);
                    continue;
                }

                uint8_t obfuscated = is_obfuscated(buffer);

                if (verbose >= LL_TRACE) {
                    log(LL_TRACE, "Received %d bytes from %s:%d to %s:%d (obfuscated=%s)\n",
                        length,
                        forward_host, forward_port, 
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
                        log(LL_ERROR, "Failed to decode packet from %s:%d\n", forward_host, forward_port);
                        continue;
                    }
                }

                if (*((uint32_t*)buffer) == WG_TYPE_HANDSHAKE_RESP) {
                    log(LL_INFO, "Received WireGuard handshake response from %s:%d (%d bytes, obfuscated=%s)\n", forward_host, forward_port,
                        length, obfuscated ? "yes" : "no");

                    // Check handshake timeout
                    if (now.tv_sec - client_entry->last_handshake_request_time.tv_sec > HANDSHAKE_TIMEOUT) {
                        log(LL_DEBUG, "Ignoring WireGuard handshake response, handshake timeout\n");
                        continue;
                    }

                    client_entry->handshaked = 1;
                    client_entry->last_handshake_time = now;
                } else if (!client_entry->handshaked) {
                    log(LL_DEBUG, "Ignoring response from %s:%d to %s:%d until the handshake is completed\n",
                        forward_host, forward_port,
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port));
                    continue;
                }

                if (!obfuscated) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, xor_key, key_length, client_entry->version);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d\n", forward_host, forward_port);
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

        if (now.tv_sec - last_cleanup_time.tv_sec >= CLEANUP_INTERVAL) {
            // Cleanup old entries
            client_entry_t *current_entry, *tmp;
            HASH_ITER(hh, conn_table, current_entry, tmp) {
                if (
                    (now.tv_sec - current_entry->last_activity_time.tv_sec >= IDLE_TIMEOUT)
                    || (!current_entry->handshaked && now.tv_sec - current_entry->last_handshake_request_time.tv_sec >= HANDSHAKE_TIMEOUT)
                ) {
                    log(LL_DEBUG, "Removing idle client %s:%d\n", inet_ntoa(current_entry->client_addr.sin_addr), ntohs(current_entry->client_addr.sin_port));
                    epoll_ctl(epfd, EPOLL_CTL_DEL, current_entry->server_sock, NULL);
                    close(current_entry->server_sock);
                    HASH_DEL(conn_table, current_entry);
                    free(current_entry);
                }
            }
            last_cleanup_time = now;
        }
    } // while (1)

    // You should never reach this point, but just in case
    return 0;
}
