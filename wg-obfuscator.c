#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
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
// PIDs of the other instances, filled by the parent process only
static pid_t *child_pids = NULL;
static int child_pids_count = 0;
// Set by the SIGHUP handler, the log file is reopened from the main loop
static volatile sig_atomic_t log_reopen_pending = 0;

// Hostname re-resolve: the blocking getaddrinfo() runs in a helper thread
#define RESOLVE_TAG_TARGET (-1)
typedef struct {
    int32_t tag;     // RESOLVE_TAG_TARGET or index into resolve_bindings
    int32_t err;     // getaddrinfo() error, 0 on success
    uint32_t addr;   // IPv4 address in network byte order
} resolve_result_t;

static int resolve_wake_rd = -1;
static int resolve_wake_wr = -1;
static int resolve_result_rd = -1;
static int resolve_result_wr = -1;
static char resolve_target_host[256];
static int resolve_target_is_name = 0;
static long resolve_interval_ms = 0;
static client_entry_t **resolve_bindings = NULL;
static int resolve_bindings_count = 0;

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
 * @brief Returns 1 if the string is a dotted IPv4 address, 0 otherwise.
 */
static int is_ipv4_literal(const char *host)
{
    struct in_addr tmp;
    return host && *host && inet_pton(AF_INET, host, &tmp) == 1;
}

/**
 * @brief Resolves a hostname to an IPv4 address.
 *
 * @param host Hostname or IPv4 literal.
 * @param out Filled with the first IPv4 address on success.
 * @return 0 on success, a getaddrinfo() error code otherwise.
 */
static int resolve_hostname(const char *host, struct in_addr *out)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *addr = NULL;
    int err = getaddrinfo(host, NULL, &hints, &addr);
    if (err != 0 || addr == NULL) {
        if (addr) {
            freeaddrinfo(addr);
        }
        return err ? err : EAI_NONAME;
    }
    *out = ((struct sockaddr_in *)addr->ai_addr)->sin_addr;
    freeaddrinfo(addr);
    return 0;
}

/**
 * @brief Resolves a hostname at startup.
 *
 * On failure: if interval_ms is 0, returns the error immediately. Otherwise
 * retries every interval_ms until the name resolves, so the daemon can start
 * before the network or DNS is up. Sleep is interruptible by SIGINT/SIGTERM.
 *
 * @param host Hostname or IPv4 literal.
 * @param out Filled with the first IPv4 address on success.
 * @param interval_ms Retry interval in milliseconds, 0 to fail immediately.
 * @param what Short label for the log, e.g. "target" or "static binding".
 * @return 0 on success, a getaddrinfo() error code if interval_ms is 0 and resolve failed.
 */
static int resolve_hostname_startup(const char *host, struct in_addr *out, long interval_ms, const char *what)
{
    while (1) {
        int err = resolve_hostname(host, out);
        if (err == 0) {
            return 0;
        }
        if (interval_ms <= 0) {
            return err;
        }
        log(LL_WARN, "Can't resolve %s hostname '%s': %s, retrying in %ld seconds",
            what, host, gai_strerror(err), interval_ms / 1000);
        struct timespec ts = {
            .tv_sec = interval_ms / 1000,
            .tv_nsec = (interval_ms % 1000) * 1000000L,
        };
        while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
        }
    }
}

/**
 * @brief Sends one resolve result to the main thread. The write is at most
 * PIPE_BUF bytes, so it is atomic and cannot interleave with another result.
 */
static void resolve_send_result(int32_t tag, int32_t err, uint32_t addr)
{
    resolve_result_t r = { .tag = tag, .err = err, .addr = addr };
    const char *p = (const char *)&r;
    size_t left = sizeof(r);
    while (left) {
        ssize_t n = write(resolve_result_wr, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        p += n;
        left -= (size_t)n;
    }
}

static void resolve_one(int32_t tag, const char *host)
{
    struct in_addr addr;
    int err = resolve_hostname(host, &addr);
    resolve_send_result(tag, err, err ? 0 : addr.s_addr);
}

/**
 * @brief Resolver thread: waits for SIGHUP (wake pipe) or the refresh interval,
 * then re-resolves every non-literal hostname. Never calls log().
 */
static void *resolve_thread_fn(void *arg)
{
    (void)arg;
    struct pollfd pfd = { .fd = resolve_wake_rd, .events = POLLIN };
    while (1) {
        int timeout = resolve_interval_ms > 0 ? (int)resolve_interval_ms : -1;
        int ret = poll(&pfd, 1, timeout);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            continue;
        }
        if (ret > 0 && (pfd.revents & POLLIN)) {
            char buf[64];
            while (read(resolve_wake_rd, buf, sizeof(buf)) > 0) {
            }
        }
        if (resolve_target_is_name) {
            resolve_one(RESOLVE_TAG_TARGET, resolve_target_host);
        }
        for (int i = 0; i < resolve_bindings_count; i++) {
            if (resolve_bindings[i] && resolve_bindings[i]->bind_host[0]) {
                resolve_one(i, resolve_bindings[i]->bind_host);
            }
        }
    }
    return NULL;
}

/**
 * @brief Wakes the resolver thread. Safe to call from a signal handler.
 */
static void resolve_wake(void)
{
    char c = 1;
    if (resolve_wake_wr < 0) {
        return;
    }
    if (write(resolve_wake_wr, &c, 1) < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        // Nothing useful can be done from a signal handler
    }
}

/**
 * @brief Applies one re-resolve result on the main thread.
 */
static void apply_resolve_result(const resolve_result_t *r, struct sockaddr_in *forward_addr)
{
    if (r->err != 0) {
        const char *name = "?";
        if (r->tag == RESOLVE_TAG_TARGET) {
            name = resolve_target_host;
        } else if (r->tag >= 0 && r->tag < resolve_bindings_count && resolve_bindings[r->tag]) {
            name = resolve_bindings[r->tag]->bind_host;
        }
        log(LL_WARN, "Can't re-resolve hostname '%s': %s", name, gai_strerror(r->err));
        return;
    }

    if (r->tag == RESOLVE_TAG_TARGET) {
        if (forward_addr->sin_addr.s_addr == r->addr) {
            return;
        }
        char old_ip[INET_ADDRSTRLEN], new_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &forward_addr->sin_addr, old_ip, sizeof(old_ip));
        forward_addr->sin_addr.s_addr = r->addr;
        inet_ntop(AF_INET, &forward_addr->sin_addr, new_ip, sizeof(new_ip));
        log(LL_INFO, "Target address changed: %s -> %s", old_ip, new_ip);

        client_entry_t *e, *tmp;
        HASH_ITER(hh, conn_table, e, tmp) {
            if (connect(e->server_sock, (struct sockaddr *)forward_addr, sizeof(*forward_addr)) < 0) {
                serror_level(LL_WARN, "Failed to update target address for client %s:%d",
                    inet_ntoa(e->client_addr.sin_addr), ntohs(e->client_addr.sin_port));
            }
        }
        return;
    }

    if (r->tag < 0 || r->tag >= resolve_bindings_count) {
        return;
    }
    client_entry_t *entry = resolve_bindings[r->tag];
    if (!entry || !entry->is_static) {
        return;
    }
    if (entry->client_addr.sin_addr.s_addr == r->addr) {
        return;
    }

    struct sockaddr_in new_addr = entry->client_addr;
    new_addr.sin_addr.s_addr = r->addr;

    client_entry_t *collision = NULL;
    HASH_FIND(hh, conn_table, &new_addr, sizeof(new_addr), collision);
    if (collision && collision != entry) {
        log(LL_WARN, "Static binding '%s' re-resolved to %s:%d, but that address is already in use",
            entry->bind_host, inet_ntoa(new_addr.sin_addr), ntohs(new_addr.sin_port));
        return;
    }

    char old_ip[INET_ADDRSTRLEN], new_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &entry->client_addr.sin_addr, old_ip, sizeof(old_ip));
    inet_ntop(AF_INET, &new_addr.sin_addr, new_ip, sizeof(new_ip));

    HASH_DEL(conn_table, entry);
    entry->client_addr.sin_addr.s_addr = r->addr;
    HASH_ADD(hh, conn_table, client_addr, sizeof(entry->client_addr), entry);
    log(LL_INFO, "Static binding '%s' address changed: %s -> %s", entry->bind_host, old_ip, new_ip);
}

/**
 * @brief Reads every queued re-resolve result. Each write is atomic, so one
 * read of sizeof(resolve_result_t) is one message.
 */
static void drain_resolve_results(struct sockaddr_in *forward_addr)
{
    resolve_result_t r;
    while (1) {
        ssize_t n = read(resolve_result_rd, &r, sizeof(r));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            serror_level(LL_WARN, "Failed to read hostname re-resolve result");
            break;
        }
        if (n == 0) {
            break;
        }
        if (n != (ssize_t)sizeof(r)) {
            log(LL_WARN, "Short read from hostname resolver (%zd bytes)", n);
            break;
        }
        apply_resolve_result(&r, forward_addr);
    }
}

/**
 * @brief Starts the resolver thread if there is at least one hostname to refresh.
 */
static void resolve_start_thread(void)
{
    int wake[2], result[2];
    pthread_t thread;

    if (!resolve_target_is_name && resolve_bindings_count == 0) {
        return;
    }

    if (pipe(wake) < 0 || pipe(result) < 0) {
        log(LL_WARN, "Can't create pipes for hostname re-resolve, periodic refresh disabled");
        return;
    }
    resolve_wake_rd = wake[0];
    resolve_wake_wr = wake[1];
    resolve_result_rd = result[0];
    resolve_result_wr = result[1];

    fcntl(resolve_wake_rd, F_SETFL, O_NONBLOCK);
    fcntl(resolve_wake_wr, F_SETFL, O_NONBLOCK);
    fcntl(resolve_result_rd, F_SETFL, O_NONBLOCK);

    if (pthread_create(&thread, NULL, resolve_thread_fn, NULL) != 0) {
        log(LL_WARN, "Can't start hostname resolver thread, periodic refresh disabled");
        close(resolve_wake_rd);
        close(resolve_wake_wr);
        close(resolve_result_rd);
        close(resolve_result_wr);
        resolve_wake_rd = resolve_wake_wr = resolve_result_rd = resolve_result_wr = -1;
        return;
    }
    pthread_detach(thread);

    if (resolve_interval_ms > 0) {
        log(LL_INFO, "Re-resolving hostnames every %ld seconds (and on SIGHUP)", resolve_interval_ms / 1000);
    } else {
        log(LL_INFO, "Re-resolving hostnames on SIGHUP");
    }
}

/**
 * @brief Remembers the PID of a forked instance.
 *
 * Called by the configuration parser for every additional section, so that the
 * parent process can forward signals to all the instances.
 *
 * @param pid PID of the forked instance.
 */
void register_child_instance(pid_t pid)
{
    pid_t *new_pids = realloc(child_pids, (child_pids_count + 1) * sizeof(pid_t));
    if (!new_pids) {
        log(LL_WARN, "Out of memory, instance with PID %ld will not receive forwarded signals", (long)pid);
        return;
    }
    child_pids = new_pids;
    child_pids[child_pids_count++] = pid;
}

#ifdef SIGHUP
/**
 * @brief Handles SIGHUP: schedules reopening of the log file.
 *
 * The actual reopening is done by the main loop, the handler only sets a flag
 * and forwards the signal to the instances of the other configuration sections.
 *
 * @param sig Signal number received by the process.
 */
static void sighup_handler(int sig)
{
    (void)sig;
    log_reopen_pending = 1;
    resolve_wake();
    // The list is filled before the handler is installed, so it can't change here
    for (int i = 0; i < child_pids_count; i++) {
        kill(child_pids[i], SIGHUP);
    }
}
#endif

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
 * @param bind_host Original hostname from the configuration, stored if it is not an IPv4 literal.
 * @return Pointer to the newly created client_entry_t structure, or NULL on failure.
 */
static client_entry_t * new_client_entry_static(obfuscator_config_t *config, struct sockaddr_in *client_addr, struct sockaddr_in *forward_addr, uint16_t local_port, const char *bind_host) {
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
    if (bind_host && !is_ipv4_literal(bind_host)) {
        strncpy(client_entry->bind_host, bind_host, sizeof(client_entry->bind_host) - 1);
    }

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
const char *version_string(void) {
#ifdef COMMIT
#ifndef ARCH
    return "WireGuard Obfuscator (commit " COMMIT " @ " WG_OBFUSCATOR_GIT_REPO ")";
#else
    return "WireGuard Obfuscator (commit " COMMIT " @ " WG_OBFUSCATOR_GIT_REPO ") (" ARCH ")";
#endif
#else
#ifndef ARCH
    return "WireGuard Obfuscator v" WG_OBFUSCATOR_VERSION;
#else
    return "WireGuard Obfuscator v" WG_OBFUSCATOR_VERSION " (" ARCH ")";
#endif
#endif
}

void print_version(void) {
    fprintf(stderr, "Starting %s\n", version_string());
}

int main(int argc, char *argv[]) {
    obfuscator_config_t config = {0};
    struct sockaddr_in 
        listen_addr, // Address for listening socket, for receiving data from the client
        forward_addr; // Address for forwarding socket, for sending data to the server
    uint8_t full_buffer[BUFFER_SIZE + PREBUFFER_SIZE];
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

    /* Start writing to the log file before validating the rest of the parameters,
       so that configuration errors are logged there too */
    log_init(config.log_file_set ? config.log_file : NULL, config.log_timestamps);

#ifdef USE_EPOLL
    struct epoll_event events[MAX_EVENTS];
#else
    struct pollfd pollfds[config.max_clients + 2];
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

    // 'allow-clean' is incompatible with static bindings: for a static binding
    // there is no way to know in advance whether the client's traffic must be obfuscated
    if (config.allow_clean && config.static_bindings_set) {
        log(LL_ERROR, "'allow-clean' cannot be used together with 'static-bindings'");
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
#ifdef SIGHUP
    signal(SIGHUP, sighup_handler);
#endif

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

    if (config.allow_clean) {
        log(LL_INFO, "Non-obfuscated (clean) clients are allowed, their traffic will be forwarded as is");
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
    resolve_interval_ms = config.resolve_interval;
    err = resolve_hostname_startup(target_host, &forward_addr.sin_addr, resolve_interval_ms, "target");
    if (err != 0) {
        log(LL_ERROR, "Can't resolve hostname '%s': %s", target_host, gai_strerror(err));
        FAILURE();
    }
    log(LL_DEBUG, "Resolved target hostname '%s' to %s", target_host, inet_ntoa(forward_addr.sin_addr));
    if (target_port <= 0 || target_port > 65535) {
        log(LL_ERROR, "Invalid target port: %d", target_port);
        FAILURE();
    }
    forward_addr.sin_port = htons(target_port);
    log(LL_INFO, "Target: %s:%d", target_host, target_port);
    strncpy(resolve_target_host, target_host, sizeof(resolve_target_host) - 1);
    resolve_target_is_name = !is_ipv4_literal(target_host);

    /* Add static bindings if provided */
    if (config.static_bindings) {
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
            err = resolve_hostname_startup(binding, &client_addr.sin_addr, resolve_interval_ms, "static binding");
            if (err != 0) {
                log(LL_ERROR, "Can't resolve hostname '%s' for static binding '%s:%s:%s': %s", 
                    binding, binding, colon1 + 1, colon2 + 1, gai_strerror(err));
                FAILURE();
            }
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

            if (!new_client_entry_static(&config, &client_addr, &forward_addr, local_port, binding)) {
                log(LL_ERROR, "Failed to create static binding: %s:%s:%s",
                    binding, colon1 + 1, colon2 + 1);
                FAILURE();
            }

            log(LL_INFO, "Added static binding: %s:%d <-> %d:obfuscator:%d <-> %s:%d", 
                binding, remote_port, config.listen_port,
                local_port, target_host, target_port);

            binding = strtok(NULL, ",");
        }
        free(config.static_bindings);
        config.static_bindings = NULL;
    }

    {
        client_entry_t *e, *tmp;
        int n = 0;
        HASH_ITER(hh, conn_table, e, tmp) {
            if (e->is_static && e->bind_host[0]) {
                n++;
            }
        }
        if (n > 0) {
            resolve_bindings = calloc((size_t)n, sizeof(*resolve_bindings));
            if (!resolve_bindings) {
                log(LL_WARN, "Out of memory, static binding hostnames will not be re-resolved");
            } else {
                HASH_ITER(hh, conn_table, e, tmp) {
                    if (e->is_static && e->bind_host[0]) {
                        resolve_bindings[resolve_bindings_count++] = e;
                    }
                }
            }
        }
        resolve_start_thread();
#ifdef USE_EPOLL
        if (resolve_result_rd >= 0) {
            struct epoll_event ev = {
                .events = EPOLLIN,
                .data.fd = resolve_result_rd
            };
            if (epoll_ctl(epfd, EPOLL_CTL_ADD, resolve_result_rd, &ev) != 0) {
                serror("epoll_ctl for hostname resolver");
                FAILURE();
            }
        }
#endif
    }

    log(LL_INFO, "WireGuard obfuscator successfully started");

    /* Main loop */
    while (1) {
        // Reopen the log file after it has been rotated, requested by SIGHUP
        if (log_reopen_pending) {
            log_reopen_pending = 0;
            log_reopen();
        }

        // Using epoll or poll to wait for events
#ifdef USE_EPOLL
        int events_n = epoll_wait(epfd, events, MAX_EVENTS, POLL_TIMEOUT);
        if (events_n < 0) {
            if (errno == EINTR) {
                // Interrupted by a signal, e.g. SIGHUP
                continue;
            }
            serror("epoll_wait");
            FAILURE();
        }
#else
        int nfds = 0;
        pollfds[nfds].fd = listen_sock;
        pollfds[nfds].events = POLLIN;
        nfds++;
        if (resolve_result_rd >= 0) {
            pollfds[nfds].fd = resolve_result_rd;
            pollfds[nfds].events = POLLIN;
            nfds++;
        }
        client_entry_t *entry, *tmp;
        HASH_ITER(hh, conn_table, entry, tmp) {
            if (nfds >= config.max_clients + 2) {
                log(LL_DEBUG, "Too many clients, cannot add more");
                break;
            }
            pollfds[nfds].fd = entry->server_sock;
            pollfds[nfds].events = POLLIN;
            nfds++;
        }
        int ret = poll(pollfds, nfds, POLL_TIMEOUT);
        if (ret < 0) {
            if (errno == EINTR) {
                // Interrupted by a signal, e.g. SIGHUP
                continue;
            }
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
            if (resolve_result_rd >= 0 && event->data.fd == resolve_result_rd) {
                drain_resolve_results(&forward_addr);
                continue;
            }
            if (event->data.fd == listen_sock) {
#else
        for (int e = 0; e < nfds; e++) if (pollfds[e].revents & POLLIN) {
            if (resolve_result_rd >= 0 && pollfds[e].fd == resolve_result_rd) {
                drain_resolve_results(&forward_addr);
                continue;
            }
            if (pollfds[e].fd == listen_sock) {
#endif
                /* *** Handle incoming data from the clients *** */
                uint8_t *buffer = full_buffer + PREBUFFER_SIZE;
                struct sockaddr_in sender_addr = {0};
                socklen_t sender_addr_len = sizeof(sender_addr);
                int length = recvfrom(listen_sock, buffer, BUFFER_SIZE, MSG_TRUNC | MSG_DONTWAIT, (struct sockaddr *)&sender_addr, &sender_addr_len);
                if (length < 0) {
                    // A readiness notification does not guarantee that there is something
                    // to read by the time we get here, so an empty socket is not an error
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        serror_level(LL_DEBUG, "recvfrom client");
                    }
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
                    length = masking_unwrap_from_client(&buffer, length, &config, client_entry, listen_sock, &sender_addr, &forward_addr, &masking_handler);
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
                    log_hexdump(LL_TRACE, obfuscated ? "X->: " : "O->: ", buffer, length);
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
                        client_entry->last_incoming_time = 0;
                        client_entry->masking_handler = masking_handler;
                    }
                    if (config.allow_clean) {
                        // Remember whether this client speaks plain WireGuard,
                        // its traffic will be forwarded as is in both directions
                        if (!obfuscated && !client_entry->client_clean) {
                            log(LL_INFO, "Client %s:%d is not obfuscated, forwarding its traffic as is",
                                inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port));
                        }
                        client_entry->client_clean = !obfuscated;
                        if (client_entry->client_clean) {
                            // No masking for clean clients
                            client_entry->masking_handler = NULL;
                        }
                    }
                    if (!obfuscated && !client_entry->client_clean) {
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

                if (!obfuscated && !client_entry->client_clean) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, config.xor_key, key_length, client_entry->version, config.max_dummy_length_data);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d (too short, length=%d)",
                            inet_ntoa(sender_addr.sin_addr), ntohs(sender_addr.sin_port), length);
                        continue;
                    }
                    length = masking_data_wrap_to_server(&buffer, length, &config, client_entry, listen_sock, &forward_addr);
                }

                log_hexdump(LL_TRACE, (!obfuscated && !client_entry->client_clean) ? "X->: " : "O->: ", buffer, length);

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
                uint8_t *buffer = full_buffer + PREBUFFER_SIZE;
                int length = recv(client_entry->server_sock, buffer, BUFFER_SIZE, MSG_TRUNC | MSG_DONTWAIT);
                if (length < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        serror_level(LL_DEBUG, "recv from server");
                    }
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
                    length = masking_unwrap_from_server(&buffer, length, &config, client_entry, listen_sock, &forward_addr);
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
                    log_hexdump(LL_TRACE, obfuscated ? "<-X: " : "<-O: ", buffer, length);
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
                    if (!obfuscated && !client_entry->client_clean) {
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
                    client_entry->client_obfuscated = !obfuscated && !client_entry->client_clean;
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

                if (!obfuscated && !client_entry->client_clean) {
                    // If the packet is not obfuscated, we need to encode it
                    length = encode(buffer, length, config.xor_key, key_length, client_entry->version, config.max_dummy_length_data);
                    if (length < 4) {
                        log(LL_ERROR, "Failed to encode packet from %s:%d", target_host, target_port);
                        continue;
                    }
                    length = masking_data_wrap_to_client(&buffer, length, &config, client_entry, listen_sock, &forward_addr);
                }
                
                log_hexdump(LL_TRACE, (!obfuscated && !client_entry->client_clean) ? "<-X: " : "<-O: ", buffer, length);

                // Send the response back to the original client
                length = sendto(listen_sock, buffer, length, 0, (struct sockaddr *)&client_entry->client_addr, sizeof(client_entry->client_addr));
                if (length < 0) {
                    serror_level(LL_DEBUG, "sendto %s:%d", inet_ntoa(client_entry->client_addr.sin_addr), ntohs(client_entry->client_addr.sin_port));
                    continue;
                }
                client_entry->last_activity_time = now;
                client_entry->last_incoming_time = now;
            } // if (event->data.fd != listen_sock)
        } // for (int e = 0; e < events_n; e++)

        if (now - last_cleanup_time >= ITERATE_INTERVAL) {
            client_entry_t *current_entry, *tmp;
            // Iterate over all client entries
            HASH_ITER(hh, conn_table, current_entry, tmp) {
                // Check if the entry is idle for too long
                uint8_t idle = now - current_entry->last_activity_time >= config.idle_timeout;
                uint8_t incoming_timeout = config.in_timeout > 0 && now - current_entry->last_incoming_time >= config.in_timeout;
                uint8_t handshake_timeout = !current_entry->handshaked && now - current_entry->last_activity_time >= HANDSHAKE_TIMEOUT;
                if ((idle || incoming_timeout || handshake_timeout) && !current_entry->is_static) { // Do not remove static entries
                    // Remove old entry
                    if (idle) {
                        log(LL_INFO, "Removing idle client %s:%d", inet_ntoa(current_entry->client_addr.sin_addr), ntohs(current_entry->client_addr.sin_port));
                    } else if (incoming_timeout) {
                        log(LL_INFO, "Removing client %s:%d due to incoming timeout", inet_ntoa(current_entry->client_addr.sin_addr), ntohs(current_entry->client_addr.sin_port));
                    } else if (handshake_timeout) {
                        log(LL_DEBUG, "Removing client %s:%d due to handshake timeout", inet_ntoa(current_entry->client_addr.sin_addr), ntohs(current_entry->client_addr.sin_port));
                    }
#ifdef USE_EPOLL
                    epoll_ctl(epfd, EPOLL_CTL_DEL, current_entry->server_sock, NULL);
#endif
                    close(current_entry->server_sock);
                    HASH_DEL(conn_table, current_entry);
                    free(current_entry);
                    continue;
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
