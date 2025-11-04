#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include "wg-obfuscator.h"
#include "config.h"
#include "obfuscation.h"
#include "uthash.h"
#include "masking.h"

// Verbosity level
int verbose = LL_DEFAULT;
// Section name (for multiple instances)
char section_name[256] = DEFAULT_INSTANCE_NAME;
// Listening socket for receiving data from the clients
static int listen_sock = 0;
// Hash table for client connections
static client_entry_t *conn_table = NULL;

#ifdef USE_EPOLL
    static int epfd = 0;
#endif

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
    log(LL_INFO, "Stopped.");
    exit(signal != -1 ? EXIT_SUCCESS : EXIT_FAILURE);
}
#define FAILURE() signal_handler(-1)

/**
 * @brief Creates a new client_entry_t structure and initializes it with the provided client and forward addresses.
 *
 * @param config Pointer to the obfuscator configuration structure.
 * @param client_addr Pointer to a struct sockaddr_in representing the client's address.
 * @param forward_addr Pointer to a struct sockaddr_in representing the address to which traffic should be forwarded.
 * @return Pointer to the newly created client_entry_t structure, or NULL on failure.
 */
static client_entry_t * new_client_entry(obfuscator_config_t *config, struct sockaddr_in *client_addr, struct sockaddr_in *forward_addr) {
    if (HASH_COUNT(conn_table) >= config->max_clients) {
        log(LL_ERROR, "Maximum number of clients reached (%d), cannot add new client", config->max_clients);
        return NULL;
    }
    client_entry_t * client_entry = malloc(sizeof(client_entry_t));
    if (!client_entry) {
        log(LL_ERROR, "Failed to allocate memory for client entry");
        return NULL;
    }
    memset(client_entry, 0, sizeof(client_entry_t));
    // Set default version (latest)
    client_entry->version = OBFUSCATION_VERSION;
    // Set the client address
    memcpy(&client_entry->client_addr, client_addr, sizeof(client_entry->client_addr));
    // Create a socket for the server connection
    client_entry->server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    // TODO: add client address to log
    if (client_entry->server_sock < 0) {
        serror("Failed to create server socket for client");
        free(client_entry);
        return NULL;
    }
#ifdef __linux__
    // Set "Don't Fragment" flag
    int optval = 1;
    if (setsockopt(client_entry->server_sock, IPPROTO_IP, IP_MTU_DISCOVER, &optval, sizeof(optval)) < 0) {
        serror("Failed to set 'don't fragment' flag for client");
        close(client_entry->server_sock);
        free(client_entry);
        return NULL;
    }
    if (config->fwmark) {
        if (setsockopt(client_entry->server_sock, SOL_SOCKET, SO_MARK, &config->fwmark, sizeof(config->fwmark)) < 0) {
            log(LL_WARN, "Failed to set 'firewall mark' for client: %s", strerror(errno));
        }
    }
#endif
    // Set the server address to the specified one
    connect(client_entry->server_sock, (struct sockaddr *)forward_addr, sizeof(*forward_addr));
    // Get the assigned port number
    socklen_t our_addr_len = sizeof(client_entry->our_addr);
    if (getsockname(client_entry->server_sock, (struct sockaddr *)&client_entry->our_addr, &our_addr_len) == -1) {
        serror("Failed to get socket port number");
        close(client_entry->server_sock);
        free(client_entry);
        return NULL;
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
 * @param config Pointer to the obfuscator configuration structure.
 * @param client_addr Pointer to a sockaddr_in structure representing the client's address.
 * @param forward_addr Pointer to a sockaddr_in structure representing the address to forward to.
 * @param local_port The local port number to connect to the server.
 * @return Pointer to the newly created client_entry_t structure, or NULL on failure.
 */
static client_entry_t * new_client_entry_static(obfuscator_config_t *config, struct sockaddr_in *client_addr, struct sockaddr_in *forward_addr, uint16_t local_port) {
    if (HASH_COUNT(conn_table) >= config->max_clients) {
        log(LL_ERROR, "Maximum number of clients reached (%d), cannot add new client", config->max_clients);
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
    // Set default version (latest)
    client_entry->version = OBFUSCATION_VERSION;
    // default masking type
    client_entry->masking_handler = config->masking_handler;
    // Set the client address
    memcpy(&client_entry->client_addr, client_addr, sizeof(client_entry->client_addr));
    // Create a socket for the server connection
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
#ifdef __linux__
    // Set "Don't Fragment" flag
    int optval = 1;
    if (setsockopt(client_entry->server_sock, IPPROTO_IP, IP_MTU_DISCOVER, &optval, sizeof(optval)) < 0) {
        serror("Failed to set 'don't fragment' flag for client %s:%d", 
            inet_ntoa(client_entry->client_addr.sin_addr), local_port);
        close(client_entry->server_sock);
        free(client_entry);
        return NULL;
    }
    if (config->fwmark) {
        if (setsockopt(client_entry->server_sock, SOL_SOCKET, SO_MARK, &config->fwmark, sizeof(config->fwmark)) < 0) {
            log(LL_WARN, "Failed to set 'firewall mark' for client %s:%d: %s",
                inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port), strerror(errno));
        }
    }

#endif
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

/**
 * @brief Prints the version information of the program.
 *
 * This function outputs the current version of the application to the standard output.
 * Typically used to inform users about the build or release version.
 */
void print_version(void) {
#ifdef COMMIT
#ifndef ARCH
    fprintf(stderr, "Starting WireGuard Obfuscator (commit " COMMIT " @ " WG_OBFUSCATOR_GIT_REPO ")\n");
#else
    fprintf(stderr, "Starting WireGuard Obfuscator (commit " COMMIT " @ " WG_OBFUSCATOR_GIT_REPO ") (" ARCH ")\n");
#endif
#else
#ifndef ARCH
    fprintf(stderr, "Starting WireGuard Obfuscator v" WG_OBFUSCATOR_VERSION "\n");
#else
    fprintf(stderr, "Starting WireGuard Obfuscator v" WG_OBFUSCATOR_VERSION " (" ARCH ")\n");
#endif
#endif
}

int main(int argc, char *argv[]) {
    obfuscator_config_t config;
    struct sockaddr_in 
        listen_addr, // Address for listening socket, for receiving data from the client
        forward_addr; // Address for forwarding socket, for sending data to the server
    uint8_t buffer[BUFFER_SIZE];
    char target_host[256] = {0};
    int target_port = -1;
    int key_length = 0;
    in_addr_t s_listen_addr_client = INADDR_ANY;
    long now, last_cleanup_time = 0;
    struct addrinfo *addr;
    int err;
    struct addrinfo hints = { // for getaddrinfo
        .ai_family = AF_INET, // IPv4
        .ai_socktype = SOCK_DGRAM, // UDP
    };

    print_version();

    if (parse_config(argc, argv, &config) != 0) {
        exit(EXIT_FAILURE);
    }

#ifdef USE_EPOLL
    struct epoll_event events[MAX_EVENTS];
#else
    struct pollfd pollfds[config.max_clients + 1];
#endif

    /* Check the parameters */
    // Check the listening port
    if (!config.listen_port_set) {
        log(LL_ERROR, "'source-lport' is not set in the configuration file");
        exit(EXIT_FAILURE);
    }

    // Check the target host and port
    if (!config.forward_host_port_set) {
        log(LL_ERROR, "'target' is not set in the configuration file");
        exit(EXIT_FAILURE);
    }

    // Check the XOR key
    if (!config.xor_key_set) {
        log(LL_ERROR, "'key' is not set in the configuration file");
        exit(EXIT_FAILURE);
    } 

    // Check the listening port
    if (!config.listen_port_set) {
        log(LL_ERROR, "'source-lport' is not set");
        exit(EXIT_FAILURE);
    }
 
    // Check the target host and port
    if (!config.forward_host_port_set) {
        log(LL_ERROR, "'target' is not set");
        exit(EXIT_FAILURE);
    } else {
        char *port_delimiter = strchr(config.forward_host_port, ':');
        if (port_delimiter == NULL) {
            log(LL_ERROR, "Invalid target host:port format: %s", config.forward_host_port);
            exit(EXIT_FAILURE);
        }
        *port_delimiter = 0;
        strncpy(target_host, config.forward_host_port, sizeof(target_host) - 1);
        target_host[sizeof(target_host) - 1] = 0; // Ensure null-termination
        target_port = atoi(port_delimiter + 1);
        if (target_port <= 0) {
            log(LL_ERROR, "Invalid target port: %s", port_delimiter + 1);
            exit(EXIT_FAILURE);
        }
    }

    // Check the key
    key_length = strlen(config.xor_key);
    if (!config.xor_key_set || key_length == 0) {
        log(LL_ERROR, "Key is not set");
        exit(EXIT_FAILURE);
    }

    // Check the client interface
    if (config.client_interface_set) {
        s_listen_addr_client = inet_addr(config.client_interface);
        if (s_listen_addr_client == INADDR_NONE) {
            err = getaddrinfo(config.client_interface, NULL, &hints, &addr);
            if (err != 0 || addr == NULL) {
                log(LL_ERROR, "Invalid source interface '%s': %s", config.client_interface, gai_strerror(err));
                exit(EXIT_FAILURE);
            }
            s_listen_addr_client = ((struct sockaddr_in *)addr->ai_addr)->sin_addr.s_addr;
            freeaddrinfo(addr);
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

#ifdef __linux__
    /* Set "Don't Fragment" flag */
    int optval = 1;
    if (setsockopt(listen_sock, IPPROTO_IP, IP_MTU_DISCOVER, &optval, sizeof(optval)) < 0) {
        serror("Failed to set 'don't fragment' flag for listening socket");
        FAILURE();
    }
    if (config.fwmark) {
        if (setsockopt(listen_sock, SOL_SOCKET, SO_MARK, &config.fwmark, sizeof(config.fwmark)) < 0) {
            log(LL_WARN, "Failed to set 'firewall mark' for listening socket: %s", strerror(errno));
        }
    }
#endif

    /* Bind the listening socket to the specified address and port */
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = s_listen_addr_client;
    listen_addr.sin_port = htons(config.listen_port);
    if (bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        serror("Failed to bind source socket to %s:%d", 
            inet_ntoa(listen_addr.sin_addr), ntohs(listen_addr.sin_port));
        FAILURE();
    }
    log(LL_INFO, "Listening on port %s:%d for source", inet_ntoa(listen_addr.sin_addr), ntohs(listen_addr.sin_port));

    if (config.masking_handler_set) {
        log(LL_INFO, "Using masking type: %s", config.masking_handler ? config.masking_handler->name : "none");
    }

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
    err = getaddrinfo(target_host, NULL, &hints, &addr);
    if (err != 0 || addr == NULL) {
        log(LL_ERROR, "Can't resolve hostname '%s': %s", target_host, gai_strerror(err));
        FAILURE();
    }
    forward_addr.sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
    freeaddrinfo(addr);
    log(LL_DEBUG, "Resolved target hostname '%s' to %s", target_host, inet_ntoa(forward_addr.sin_addr));
    if (target_port <= 0 || target_port > 65535) {
        log(LL_ERROR, "Invalid target port: %d", target_port);
        FAILURE();
    }
    forward_addr.sin_port = htons(target_port);
    log(LL_INFO, "Target: %s:%d", target_host, target_port);

    /* Add static bindings if provided */
    if (config.static_bindings[0]) {
        // Parse static bindings
        char *binding = strtok(config.static_bindings, ",");
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
            err = getaddrinfo(binding, NULL, &hints, &addr);
            if (err != 0 || addr == NULL) {
                log(LL_ERROR, "Can't resolve hostname '%s' for static binding '%s:%s:%s': %s", 
                    binding, binding, colon1 + 1, colon2 + 1, gai_strerror(err));
                FAILURE();
            }
            client_addr.sin_addr = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
            freeaddrinfo(addr);
            log(LL_DEBUG, "Resolved static binding hostname '%s' to %s", binding, inet_ntoa(client_addr.sin_addr));
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

            if (!new_client_entry_static(&config, &client_addr, &forward_addr, local_port)) {
                log(LL_ERROR, "Failed to create static binding: %s:%s:%s",
                    binding, colon1 + 1, colon2 + 1);
                FAILURE();
            }

            log(LL_INFO, "Added static binding: %s:%d <-> %d:obfuscator:%d <-> %s:%d", 
                binding, remote_port, config.listen_port,
                local_port, target_host, target_port);

            binding = strtok(NULL, ",");
        }
    }

    log(LL_INFO, "WireGuard obfuscator successfully started");

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
            if (nfds >= config.max_clients) {
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
                int length = recvfrom(listen_sock, buffer, BUFFER_SIZE, MSG_TRUNC, (struct sockaddr *)&sender_addr, &sender_addr_len);
                if (length < 0) {
                    serror_level(LL_DEBUG, "recvfrom client");
                    continue;
                }
                if (length > BUFFER_SIZE) {
                    log(LL_DEBUG, "Received packet from %s:%d is too large (%d bytes), while buffer size is %d bytes, ignoring",
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length, BUFFER_SIZE);
                    continue;
                }

                // Find the client entry if any
                client_entry_t *client_entry;
                HASH_FIND(hh, conn_table, &sender_addr, sizeof(sender_addr), client_entry);

                uint8_t obfuscated = length >= 4 && is_obfuscated(buffer);
                // Is it masked packet maybe?
                masking_handler_t *masking_handler = config.masking_handler;
                if (obfuscated) {
                    length = masking_unwrap_from_client(buffer, length, &config, client_entry, listen_sock, &sender_addr, &forward_addr, &masking_handler);
                    if (length <= 0) {
                        // Nothing to do
                        continue;
                    }
                }
                // Check the length
                if (length < 4) {
                    log(LL_DEBUG, "Received too short packet from %s:%d (%d bytes), ignoring", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length);
                    continue;
                }

                uint8_t version = client_entry ? client_entry->version : OBFUSCATION_VERSION;

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
                    int original_length = length;
                    length = decode(buffer, length, config.xor_key, key_length, &version);
                    if (length < 4 || length > original_length) {
                        log(LL_DEBUG, "Failed to decode packet from %s:%d (original_length=%d, decoded_length=%d)",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), original_length, length);
                        continue;
                    }
                }

                // Is it handshake?
                if (WG_TYPE(buffer) == WG_TYPE_HANDSHAKE) {
                    log(LL_DEBUG, "Received WireGuard handshake from %s:%d to %s:%d (%d bytes, obfuscated=%s)",
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        target_host, target_port,
                        length, 
                        obfuscated ? "yes" : "no");

                    if (!client_entry) {
                        client_entry = new_client_entry(&config, &sender_addr, &forward_addr);
                        if (!client_entry) {
                            continue;
                        }
                        client_entry->last_activity_time = now;
                        client_entry->masking_handler = masking_handler;
                    }
                    if (!obfuscated) {
                        masking_on_handshake_req_from_client(&config, client_entry, listen_sock, &sender_addr, &forward_addr);
                    }
                    client_entry->handshake_direction = DIR_CLIENT_TO_SERVER;
                    client_entry->last_handshake_request_time = now;
                }
                // Is it handshake response?
                else if (WG_TYPE(buffer) == WG_TYPE_HANDSHAKE_RESP) {
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

                    if (client_entry->handshake_direction != DIR_SERVER_TO_CLIENT) {
                        log(LL_DEBUG, "Received handshake response from %s:%d to %s:%d, but the handshake direction is not set to server-to-client",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                            target_host, target_port);
                        continue;;
                    }

                    log(!client_entry->handshaked ? LL_INFO : LL_DEBUG, "Handshake established with %s:%d to %s:%d (reverse)",
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        target_host, target_port);
                    client_entry->handshaked = 1;
                    client_entry->client_obfuscated = obfuscated;
                    client_entry->server_obfuscated = !obfuscated;
                    client_entry->last_handshake_time = now;
                }
                // If it's not a handshake or handshake response, connection is not established yet
                else if (!client_entry || !client_entry->handshaked) {
                    log(LL_DEBUG, "Ignoring data (packet type #%u) from %s:%d to %s:%d until the handshake is completed",
                        WG_TYPE(buffer),
                        inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port),
                        target_host, target_port);
                    continue;
                }

                // Version downgrade check
                if (version < client_entry->version) {
                    log(LL_WARN, "Client %s:%d uses old obfuscation version, downgrading from %d to %d", inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), 
                        client_entry->version, version);
                    client_entry->version = version;
                }

                if (!obfuscated) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, config.xor_key, key_length, client_entry->version, config.max_dummy_length_data);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d (too short, length=%d)",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length);
                        continue;
                    }
                    length = masking_data_wrap_to_server(buffer, length, &config, client_entry, listen_sock, &forward_addr);
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

                length = send(client_entry->server_sock, buffer, length, 0);
                if (length < 0) {
                    serror_level(LL_DEBUG, "sendto %s:%d", target_host, target_port);
                    continue;
                }
                client_entry->last_activity_time = now;
            } else { // if (event->data.fd == listen_sock)
                /* *** Handle data from the server *** */
#ifdef USE_EPOLL
                client_entry_t *client_entry = event->data.ptr;
#else
                client_entry_t *client_entry = find_by_server_sock(pollfds[e].fd);
#endif
                int length = recv(client_entry->server_sock, buffer, BUFFER_SIZE, MSG_TRUNC);
                if (length < 0) {
                    serror_level(LL_DEBUG, "recv from server");
                    continue;
                }
                if (length > BUFFER_SIZE) {
                    log(LL_DEBUG, "Received packet from %s:%d is too large (%d bytes), while buffer size is %d bytes, ignoring",
                        target_host, target_port, length, BUFFER_SIZE);
                    continue;
                }
                uint8_t obfuscated = length >= 4 && is_obfuscated(buffer);
                if (obfuscated) {
                    // Is it masked packet maybe?
                    length = masking_unwrap_from_server(buffer, length, &config, client_entry, listen_sock, &forward_addr);
                    if (length <= 0) {
                        // Nothing to do
                        continue;
                    }
                }
                // Check the length
                if (length < 4) {
                    log(LL_DEBUG, "Received too short packet from %s:%d (%d bytes), ignoring", target_host, target_port, length);
                    continue;
                }

                uint8_t version = client_entry->version;

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
                    int original_length = length;
                    length = decode(buffer, length, config.xor_key, key_length, &version);
                    if (length < 4 || length > original_length) {
                        log(LL_DEBUG, "Failed to decode packet from %s:%d (original_length=%d, decoded_length=%d)", target_host, target_port, original_length, length);
                        continue;
                    }
                }

                // Is it handshake?
                if (WG_TYPE(buffer) == WG_TYPE_HANDSHAKE) {
                    log(LL_DEBUG, "Received WireGuard handshake from %s:%d to %s:%d (%d bytes, obfuscated=%s)",
                        target_host, target_port,
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        length, 
                        obfuscated ? "yes" : "no");
                    if (!obfuscated) {
                        // Send STUN binding request before the obfuscated handshake
                        masking_on_handshake_req_from_server(&config, client_entry, listen_sock, &client_entry->client_addr, &forward_addr);
                    }
                    client_entry->handshake_direction = DIR_SERVER_TO_CLIENT;
                    client_entry->last_handshake_request_time = now;
                }
                // Is it handshake response?
                else if (WG_TYPE(buffer) == WG_TYPE_HANDSHAKE_RESP) {
                    log(LL_DEBUG, "Received WireGuard handshake response from %s:%d to %s:%d (%d bytes, obfuscated=%s)",
                        target_host, target_port,
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        length, obfuscated ? "yes" : "no");

                    // Check handshake timeout
                    if (now - client_entry->last_handshake_request_time > HANDSHAKE_TIMEOUT) {
                        log(LL_DEBUG, "Ignoring WireGuard handshake response, handshake timeout");
                        continue;
                    }

                    if (client_entry->handshake_direction != DIR_CLIENT_TO_SERVER) {
                        log(LL_DEBUG, "Received handshake response from %s:%d to %s:%d, but the handshake direction is not set to client-to-server",
                            target_host, target_port,
                            inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port));
                        continue;
                    }

                    log(!client_entry->handshaked ? LL_INFO : LL_DEBUG, "Handshake established with %s:%d to %s:%d (direct)",
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port),
                        target_host, target_port);
                    if (!client_entry->handshaked && client_entry->masking_handler && !config.masking_handler_set) {
                        log(LL_INFO, "Autodetected masking handler for client %s:%d: %s", inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port), client_entry->masking_handler->name);
                    }
                    client_entry->handshaked = 1;
                    client_entry->client_obfuscated = !obfuscated;
                    client_entry->server_obfuscated = obfuscated;
                    client_entry->last_handshake_time = now;
                }
                // If it's not a handshake or handshake response, connection is not established yet
                else if (!client_entry->handshaked) {
                    log(LL_DEBUG, "Ignoring response (packet type #%u) from %s:%d to %s:%d until the handshake is completed",
                        WG_TYPE(buffer),
                        target_host, target_port,
                        inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port));
                    continue;
                }

                // Version downgrade check
                if (version < client_entry->version) {
                    log(LL_WARN, "Server %s:%d uses old obfuscation version, downgrading from %d to %d", 
                        target_host, target_port, client_entry->version, version);
                    client_entry->version = version;
                }

                if (!obfuscated) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, config.xor_key, key_length, client_entry->version, config.max_dummy_length_data);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d", target_host, target_port);
                        continue;
                    }
                    length = masking_data_wrap_to_client(buffer, length, &config, client_entry, listen_sock, &forward_addr);
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
                length = sendto(listen_sock, buffer, length, 0, (struct sockaddr *)&client_entry->client_addr, sizeof(client_entry->client_addr));
                if (length < 0) {
                    serror_level(LL_DEBUG, "sendto %s:%d", inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port));
                    continue;
                }
                client_entry->last_activity_time = now;
            } // if (event->data.fd != listen_sock)
        } // for (int e = 0; e < events_n; e++)

        if (now - last_cleanup_time >= ITERATE_INTERVAL) {
            client_entry_t *current_entry, *tmp;
            // Iterate over all client entries
            HASH_ITER(hh, conn_table, current_entry, tmp) {
                // Check if the entry is idle for too long
                if (
                    (
                        (now - current_entry->last_activity_time >= config.idle_timeout)
                        || (!current_entry->handshaked && (now - current_entry->last_activity_time >= HANDSHAKE_TIMEOUT))
                    ) && !current_entry->is_static // Do not remove static entries
                ) {
                    // Remove old entry
                    log(current_entry->handshaked ? LL_INFO : LL_DEBUG, "Removing idle client %s:%d (handshaked=%s)", inet_ntoa(current_entry->client_addr.sin_addr), ntohs(current_entry->client_addr.sin_port), 
                        current_entry->handshaked ? "yes" : "no");
#ifdef USE_EPOLL
                    epoll_ctl(epfd, EPOLL_CTL_DEL, current_entry->server_sock, NULL);
#endif
                    close(current_entry->server_sock);
                    HASH_DEL(conn_table, current_entry);
                    free(current_entry);
                }

                // Check if we need to call masking timer
                if (current_entry->masking_handler && current_entry->masking_handler->timer_interval_s > 0
                    && now - current_entry->last_masking_timer_time >= current_entry->masking_handler->timer_interval_s * 1000) {
                    current_entry->last_masking_timer_time = now;
                    masking_on_timer(&config, current_entry, listen_sock, &forward_addr);
                }
            }
            // Update the last cleanup time
            last_cleanup_time = now;
        }
    } // while (1)

    // You should never reach this point, but just in case
    return 0;
}
