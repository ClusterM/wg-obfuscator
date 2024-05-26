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
#include "wg-obfuscator.h"
#include "commit.h"

#define print(level, fmt, ...) { if (verbose >= level) fprintf(stderr, fmt, ##__VA_ARGS__); }
#define debug_print(fmt, ...) print(4, fmt, ##__VA_ARGS__)

static int listen_sock = 0, forward_sock = 0;

// main parameters (TODO: IPv6?)
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

static void read_config_file(char *filename)
{
    // Read configuration from file
    char line[256];
    FILE *config_file = fopen(filename, "r");
    if (config_file == NULL) {
        perror("config file");
        exit(EXIT_FAILURE);
    }
    int listen_port_set = 0;
    int forward_host_port_set = 0;
    int xor_key_set = 0;   
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
        // debug_print("Read line: '%s'\n", line);

        // Parse key-value pairs
        char *key = strtok(line, "=");
        // Trim leading and trailing spaces
        while (strlen(key) && (key[0] == ' ' || key[0] == '\t' || key[0] == '\r' || key[0] == '\n')) {
            key++;
        }
        while (strlen(key) && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t' || key[strlen(key) - 1] == '\r' || key[strlen(key) - 1] == '\n')) {
            key[strlen(key) - 1] = 0;
        }
        // debug_print("Key: '%s'\n", key);
        char *value = strtok(NULL, "=");
        if (value == NULL) {
            fprintf(stderr, "Invalid configuration line: %s\n", line);
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
            fprintf(stderr, "Invalid configuration line: %s\n", line);
            exit(EXIT_FAILURE);
        }
        // debug_print("Value: '%s'\n", value);

        if (strcmp(key, "source-lport") == 0) {
            listen_port = atoi(value);
            listen_port_set = 1;
        } else if (strcmp(key, "target") == 0) {
            strncpy(forward_host_port, value, sizeof(forward_host_port) - 1);
            forward_host_port_set = 1;
        } else if (strcmp(key, "key") == 0) {
            strncpy(xor_key, value, sizeof(xor_key) - 1);
            xor_key_set = 1;
        } else if (strcmp(key, "source-if") == 0) {
            strncpy(client_interface, value, sizeof(client_interface) - 1);
        } else if (strcmp(key, "target-if") == 0) {
            strncpy(forward_interface, value, sizeof(forward_interface) - 1);
        } else if (strcmp(key, "source") == 0) {
            strncpy(client_fixed_addr_port, value, sizeof(client_fixed_addr_port) - 1);
        } else if (strcmp(key, "target-lport") == 0) {
            server_local_port = atoi(value);
        } else if (strcmp(key, "verbose") == 0) {
            strncpy(verbose_str, value, sizeof(verbose_str) - 1);
        } else {
            fprintf(stderr, "Unknown configuration key: '%s'\n", key);
            exit(EXIT_FAILURE);
        }
    }
    fclose(config_file);
    if (!listen_port_set) {
        fprintf(stderr, "'source-lport' is not set in the configuration file\n");
        exit(EXIT_FAILURE);
    }
    if (!forward_host_port_set) {
        fprintf(stderr, "'target' is not set in the configuration file\n");
        exit(EXIT_FAILURE);
    }
    if (!xor_key_set) {
        fprintf(stderr, "'key' is not set in the configuration file\n");
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
    .doc = "WireGuard Obfuscator " VERSION " (commit" COMMIT " @ " GIT_REPO ")"
};

// XOR the data with the key
static void xor_data(uint8_t *data, size_t length, char *key, size_t key_length) {
    for (size_t i = 0; i < length; ++i) {
        data[i] ^= key[i % key_length];
    }
}

// Handle signals
static void signal_handler(int signal) {
    switch (signal) {
        case SIGINT:
        case SIGTERM:
            // Stop the main loop
            if (listen_sock) {
                close(listen_sock);
            }
            if (forward_sock) {
                close(forward_sock);
            }
            print(1, "Stopped.\n");
            exit(EXIT_SUCCESS);
            break;
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in 
        listen_addr, // Address for listening socket, for receiving data from the client
        forward_addr, // Address for forwarding socket, for sending data to the server
        last_sender_addr_temp, // Address of the client to which we will send the response (temporary)
        last_sender_addr, // Address of the client to which we will send the response
        forward_client_addr, // Address of the forward socket, for receiving data from the server
        forward_server_addr; // Address of the server, from which we will receive the response
    uint8_t buffer[BUFFER_SIZE];
    int last_sender_set = 0;
    time_t last_handshake_time = 0;
    char forward_host[256] = {0};
    int forward_port = -1;
    size_t key_length = 0;
    unsigned long s_listen_addr_client = INADDR_ANY;
    unsigned long s_listen_addr_forward = INADDR_ANY;

    // Parse command line arguments
    if (argc == 1) {
        fprintf(stderr, "No arguments provided, use \"%s --help\" command for usage information\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    if (argp_parse(&argp, argc, argv, 0, 0, 0) != 0) {
        fprintf(stderr, "Failed to parse command line arguments\n");
        exit(EXIT_FAILURE);
    }
  
    // Check the parameters
    key_length = strlen(xor_key);
    if (listen_port < 0) {
        fprintf(stderr, "'source-lport' is not set\n");
        exit(EXIT_FAILURE);
    }
    if (client_fixed_addr_port[0]) {
        char *port_delimiter = strchr(client_fixed_addr_port, ':');
        if (port_delimiter == NULL) {
            fprintf(stderr, "Invalid source ip:port format: %s\n", client_fixed_addr_port);
            exit(EXIT_FAILURE);
        }
        *port_delimiter = 0;
        last_sender_addr.sin_addr.s_addr = inet_addr(client_fixed_addr_port);
        if (last_sender_addr.sin_addr.s_addr == INADDR_NONE) {
            fprintf(stderr, "Invalid source ip: %s\n", client_fixed_addr_port);
            exit(EXIT_FAILURE);
        }
        last_sender_addr.sin_port = htons(atoi(port_delimiter + 1));
        if (last_sender_addr.sin_port <= 0) {
            fprintf(stderr, "Invalid source port: %s\n", port_delimiter + 1);
            exit(EXIT_FAILURE);
        }
        last_sender_addr.sin_family = AF_INET;
        last_sender_set = 1;
        print(1, "Source: %s:%d\n", inet_ntoa(last_sender_addr.sin_addr), ntohs(last_sender_addr.sin_port));
    } else {
        print(1, "Source: any/auto\n");
    }  
    if (!forward_host_port[0]) {
        fprintf(stderr, "'target' is not set\n");
        exit(EXIT_FAILURE);
    } else {
        char *port_delimiter = strchr(forward_host_port, ':');
        if (port_delimiter == NULL) {
            fprintf(stderr, "Invalid target host:port format: %s\n", forward_host_port);
            exit(EXIT_FAILURE);
        }
        *port_delimiter = 0;
        strncpy(forward_host, forward_host_port, sizeof(forward_host) - 1);
        forward_port = atoi(port_delimiter + 1);
        if (forward_port <= 0) {
            fprintf(stderr, "Invalid target port: %s\n", port_delimiter + 1);
            exit(EXIT_FAILURE);
        }
        print(1, "Target: %s:%d\n", forward_host, forward_port);
    } 
    if (key_length == 0) {
        fprintf(stderr, "Key is not set\n");
        exit(EXIT_FAILURE);
    }
    if (client_interface[0]) {
        s_listen_addr_client = inet_addr(client_interface);
    }
    if (forward_interface[0]) {
        s_listen_addr_forward = inet_addr(forward_interface);
    }
    if (s_listen_addr_client == INADDR_NONE) {
        fprintf(stderr, "Invalid source interface: %s\n", client_interface);
        exit(EXIT_FAILURE);
    }
    if (s_listen_addr_forward == INADDR_NONE) {
        fprintf(stderr, "Invalid target interface: %s\n", forward_interface);
        exit(EXIT_FAILURE);
    }
    if (verbose_str[0]) {
        verbose = atoi(verbose_str);
        if (verbose < 0 || verbose > 4) {
            fprintf(stderr, "Invalid verbosity level: %s\n", verbose_str);
            exit(EXIT_FAILURE);
        }
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create listening socket
    if ((listen_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("source socket");
        exit(EXIT_FAILURE);
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = s_listen_addr_client;
    listen_addr.sin_port = htons(listen_port);

    if (bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("source socket bind");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }
    print(1, "Listening on port %s:%d for source\n", inet_ntoa(listen_addr.sin_addr), ntohs(listen_addr.sin_port));

    // Create forwarding socket and bind it to the same port
    if ((forward_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("target socket");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // Create a client address for the forward socket
    memset(&forward_client_addr, 0, sizeof(forward_client_addr));
    forward_client_addr.sin_family = AF_INET;
    forward_client_addr.sin_addr.s_addr = s_listen_addr_forward;
    forward_client_addr.sin_port = server_local_port ? htons(server_local_port) : 0;

    if (bind(forward_sock, (struct sockaddr *)&forward_client_addr, sizeof(forward_client_addr)) < 0) {
        perror("target socket bind");
        close(listen_sock);
        close(forward_sock);
        exit(EXIT_FAILURE);
    }

    // Get the assigned port number
    socklen_t forward_client_addr_len = sizeof(forward_client_addr);
    if (getsockname(forward_sock, (struct sockaddr *)&forward_client_addr, &forward_client_addr_len) == -1) {
        perror("failed to get socket port number");
        close(listen_sock);
        close(forward_sock);
        exit(EXIT_FAILURE);
    }
    print(1, "Listening on port %s:%d for target\n", inet_ntoa(forward_client_addr.sin_addr), ntohs(forward_client_addr.sin_port));

    // Set up forward address
    memset(&forward_addr, 0, sizeof(forward_addr));
    forward_addr.sin_family = AF_INET;
    struct hostent *host = gethostbyname(forward_host);
    if (host == NULL) {
        perror("can't resolve hostname");
        close(listen_sock);
        close(forward_sock);
        exit(EXIT_FAILURE);
    }
    forward_addr.sin_addr = *(struct in_addr *)host->h_addr;
    forward_addr.sin_port = htons(forward_port);

    print(1, "WireGuard obfuscator successfully started\n");

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        FD_SET(forward_sock, &read_fds);

        int max_fd = (listen_sock > forward_sock) ? listen_sock : forward_sock;

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listen_sock, &read_fds)) {
            socklen_t last_sender_addr_temp_len = sizeof(last_sender_addr_temp);
            uint8_t is_handshake = 0;
            ssize_t received = recvfrom(listen_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&last_sender_addr_temp, &last_sender_addr_temp_len);
            if (received < 0) {
                perror("recvfrom");
                continue;
            }

            // Store the last sender address
            if (received >= sizeof(wg_signature) && memcmp(buffer, wg_signature, sizeof(wg_signature)) == 0) {
                print(2, "Received WireGuard handshake (non-obfuscated) from %s:%d\n", inet_ntoa(last_sender_addr_temp.sin_addr), ntohs(last_sender_addr_temp.sin_port));
                is_handshake = 1;
            }

            debug_print("Received %ld bytes from %s:%d\n", received, inet_ntoa(last_sender_addr_temp.sin_addr), ntohs(last_sender_addr_temp.sin_port));
            debug_print("O->: ");
            for (int i = 0; i < received; ++i) {
                debug_print("%02X ", buffer[i]);
            }
            debug_print("\n");

            // XOR the data
            xor_data(buffer, received, xor_key, key_length);
            debug_print("X->: ");
            for (int i = 0; i < received; ++i) {
                debug_print("%02X ", buffer[i]);
            }
            debug_print("\n");

            // Store the last sender address
            if (received >= sizeof(wg_signature) && memcmp(buffer, wg_signature, sizeof(wg_signature)) == 0) {
                print(2, "Received WireGuard handshake (obfuscated) from %s:%d\n", inet_ntoa(last_sender_addr_temp.sin_addr), ntohs(last_sender_addr_temp.sin_port));
                is_handshake = 1;
            }

            if (is_handshake) {
                last_handshake_time = time(NULL);
            } else {
                last_handshake_time = 0;
            }

            // Send the data to the forward port if it's allowed client
            if (client_fixed_addr_port[0] && (last_sender_addr_temp.sin_addr.s_addr != last_sender_addr.sin_addr.s_addr || last_sender_addr_temp.sin_port != last_sender_addr.sin_port))
            {
                print(3, "Fixed source address mismatch: %s:%d != %s:%d\n", inet_ntoa(last_sender_addr_temp.sin_addr), ntohs(last_sender_addr_temp.sin_port), inet_ntoa(last_sender_addr.sin_addr), ntohs(last_sender_addr.sin_port));
            } else if (!client_fixed_addr_port[0] && !is_handshake && (last_sender_addr_temp.sin_addr.s_addr != last_sender_addr.sin_addr.s_addr || last_sender_addr_temp.sin_port != last_sender_addr.sin_port))
            {
                print(3, "Ignoring data from %s:%d until the handshake is completed\n", inet_ntoa(last_sender_addr_temp.sin_addr), ntohs(last_sender_addr_temp.sin_port));
            } else {
                sendto(forward_sock, buffer, received, 0, (struct sockaddr *)&forward_addr, sizeof(forward_addr));
            }
        }

        if (FD_ISSET(forward_sock, &read_fds)) {
            socklen_t forward_server_addr_len = sizeof(forward_server_addr);
            ssize_t received = recvfrom(forward_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&forward_server_addr, &forward_server_addr_len);
            if (received < 0) {
                perror("recvfrom");
                continue;
            }

            // Ignore if the sender is not the server
            if (memcmp(&forward_server_addr, &forward_addr, sizeof(forward_server_addr)) != 0) {
                continue;
            }

            debug_print("Received %ld bytes from %s:%d\n", received, inet_ntoa(forward_server_addr.sin_addr), ntohs(forward_server_addr.sin_port));
            debug_print("<-O: ");
            for (int i = 0; i < received; ++i) {
                debug_print("%02X ", buffer[i]);
            }
            debug_print("\n");

            int need_to_set_client_addr = 0;
            // Check if the response is a WireGuard handshake response
            if (received >= sizeof(wg_signature_resp) && memcmp(buffer, wg_signature_resp, sizeof(wg_signature_resp)) == 0) {
                print(2, "Received WireGuard handshake response (non-obfuscated) from %s:%d\n", inet_ntoa(forward_server_addr.sin_addr), ntohs(forward_server_addr.sin_port));
                need_to_set_client_addr = 1;
            }

            // XOR the data
            xor_data(buffer, received, xor_key, key_length);
            debug_print("<-X: ");
            for (int i = 0; i < received; ++i) {
                debug_print("%02X ", buffer[i]);
            }
            debug_print("\n");

            // Check if the response is a WireGuard handshake response
            if (received >= sizeof(wg_signature_resp) && memcmp(buffer, wg_signature_resp, sizeof(wg_signature_resp)) == 0) {
                print(2, "Received WireGuard handshake response (obfuscated) from %s:%d\n", inet_ntoa(forward_server_addr.sin_addr), ntohs(forward_server_addr.sin_port));
                need_to_set_client_addr = 1;
            }

            // Store the last sender address
            if (need_to_set_client_addr && !client_fixed_addr_port[0]) {
                if (time(NULL) - last_handshake_time < HANDSHAKE_TIMEOUT) {
                    if (last_sender_addr_temp.sin_addr.s_addr != last_sender_addr.sin_addr.s_addr || last_sender_addr_temp.sin_port != last_sender_addr.sin_port) {
                        memcpy(&last_sender_addr, &last_sender_addr_temp, sizeof(last_sender_addr));
                        last_sender_set = 1;
                        print(2, "Client address set to %s:%d\n", inet_ntoa(last_sender_addr.sin_addr), ntohs(last_sender_addr.sin_port));
                    }
                } else {
                    if (last_handshake_time) {
                        print(3, "Ignoring WireGuard handshake response, handshake timeout\n");
                    } else {
                        print(3, "Ignoring WireGuard handshake response, no handshake request\n");                    
                    }
                }
            }

            // Send the response back to the original client
            if (last_sender_set) {
                sendto(listen_sock, buffer, received, 0, (struct sockaddr *)&last_sender_addr, sizeof(last_sender_addr));
            } else {
                print(3, "No source address set, ignoring the response\n");
            }
        }
    }

    close(listen_sock);
    close(forward_sock);

    print(1, "Stopped.\n");

    return 0;
}
