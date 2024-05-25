#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>

#define BUFFER_SIZE 2048
#define HANDSHAKE_TIMEOUT 5

#ifdef DEBUG
#define debug_print(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...)
#endif

// WireGuard handshake signature
static const uint8_t wg_signature[] = {0x01, 0x00, 0x00, 0x00};
static const uint8_t wg_signature_resp[] = {0x02, 0x00, 0x00, 0x00};

static int listen_sock = 0, forward_sock = 0;

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
            fprintf(stderr, "Stopped.\n");
            exit(EXIT_SUCCESS);
            break;
        default:
            debug_print("Received signal %d\n", signal);
            break;
    }
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    debug_print("DEBUG mode enabled\n");

    int listen_port = 1;
    char forward_host[256] = {0};
    int forward_port = -1;
    char xor_key[256] = {0};
    size_t key_length = 0;

    // optional parameters
    unsigned long s_addr_client = INADDR_ANY;
    unsigned long s_addr_forward = INADDR_ANY;

    if (argc == 5) {
        // Read configuration from command line arguments
        listen_port = atoi(argv[1]);
        strncat(forward_host, argv[2], sizeof(forward_host)-1);
        forward_port = atoi(argv[3]);
        strncat(xor_key, argv[4], sizeof(xor_key)-1);
    } else if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        // Read configuration from file
        char line[256];
        FILE *config_file = fopen(argv[2], "r");
        if (config_file == NULL) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
        int listen_port_set = 0;
        int forward_host_set = 0;
        int forward_port_set = 0;
        int xor_key_set = 0;   
        while (fgets(line, sizeof(line), config_file)) {
            // Remove trailing newlines, carriage returns, spaces and tabs
            while (strlen(line) && (line[strlen(line) - 1] == '\n' || line[strlen(line) - 1] == '\r' 
                || line[strlen(line) - 1] == ' ' || line[strlen(line) - 1] == '\t')) {
                line[strlen(line) - 1] = 0;
            }
            // Ignore comments
            int comment_index = strcspn(line, "#");
            if (comment_index > 0) {
                line[comment_index] = 0;
            }
            // Skip empty lines or with spaces only
            if (strspn(line, " \t\n") == strlen(line)) {
                continue;
            }
            
            // Parse key-value pairs
            char *key = strtok(line, "=");
            // Trim leading and trailing spaces
            while (strlen(key) && (key[0] == ' ' || key[0] == '\t')) {
                key++;
            }
            while (strlen(key) && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t')) {
                key[strlen(key) - 1] = 0;
            }
            char *value = strtok(NULL, "=");
            // Trim leading and trailing spaces
            while (strlen(value) && (value[0] == ' ' || value[0] == '\t')) {
                value++;
            }
            while (strlen(value) && (value[strlen(value) - 1] == ' ' || value[strlen(value) - 1] == '\t')) {
                value[strlen(value) - 1] = 0;
            }
            if (value == NULL) {
                fprintf(stderr, "Invalid configuration line: %s\n", line);
                exit(EXIT_FAILURE);
            }

            if (strcmp(key, "listen_port") == 0) {
                listen_port = atoi(value);
                listen_port_set = 1;
            } else if (strcmp(key, "forward_host") == 0) {
                strncat(forward_host, value, sizeof(forward_host)-1);
                forward_host_set = 1;
            } else if (strcmp(key, "forward_port") == 0) {
                forward_port = atoi(value);
                forward_port_set = 1;
            } else if (strcmp(key, "key") == 0) {
                strncat(xor_key, value, sizeof(xor_key)-1);
                key_length = strlen(xor_key);
                xor_key_set = 1;
            } else if (strcmp(key, "client_interface") == 0) {
                s_addr_client = inet_addr(value);
            } else if (strcmp(key, "forward_interface") == 0) {
                s_addr_forward = inet_addr(value);
            } else {
                fprintf(stderr, "Unknown configuration key: %s\n", key);
                exit(EXIT_FAILURE);
            }
        }
        fclose(config_file);
        if (!listen_port_set) {
            fprintf(stderr, "listen_port is not set in the configuration file\n");
            exit(EXIT_FAILURE);
        }
        if (!forward_host_set) {
            fprintf(stderr, "forward_host is not set in the configuration file\n");
            exit(EXIT_FAILURE);
        }
        if (!forward_port_set) {
            fprintf(stderr, "forward_port is not set in the configuration file\n");
            exit(EXIT_FAILURE);
        }
        if (!xor_key_set) {
            fprintf(stderr, "key is not set in the configuration file\n");
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "Usage: %s <listen_port> <forward_host> <forward_port> <key>\n", argv[0]);
        fprintf(stderr, "Usage: %s -c <config_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }    
    key_length = strlen(xor_key);

    if (listen_port < 0) {
        fprintf(stderr, "Source port is not set\n");
        exit(EXIT_FAILURE);
    }
    if (!forward_host[0]) {
        fprintf(stderr, "Forward host is not set\n");
        exit(EXIT_FAILURE);
    }
    if (forward_port < 0) {
        fprintf(stderr, "Forward port is not set\n");
        exit(EXIT_FAILURE);
    }
    if (key_length == 0) {
        fprintf(stderr, "Key is not set\n");
        exit(EXIT_FAILURE);
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

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

    fprintf(stderr, "Starting WireGuard obfuscator (c) 2024 by Alexey Cluster\n");

    // Create listening socket
    if ((listen_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = s_addr_client;
    listen_addr.sin_port = htons(listen_port);

    if (bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("bind");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Listening on port %s:%d for client\n", inet_ntoa(listen_addr.sin_addr), ntohs(listen_addr.sin_port));

    // Create forwarding socket and bind it to the same port
    if ((forward_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        close(listen_sock);
        exit(EXIT_FAILURE);
    }

    // Create a client address for the forward socket
    memset(&forward_client_addr, 0, sizeof(forward_client_addr));
    forward_client_addr.sin_family = AF_INET;
    forward_client_addr.sin_addr.s_addr = s_addr_forward;
    forward_client_addr.sin_port = 0; // Let the system assign an available port

    if (bind(forward_sock, (struct sockaddr *)&forward_client_addr, sizeof(forward_client_addr)) < 0) {
        perror("bind");
        close(listen_sock);
        close(forward_sock);
        exit(EXIT_FAILURE);
    }

    // Get the assigned port number
    socklen_t forward_client_addr_len = sizeof(forward_client_addr);
    if (getsockname(forward_sock, (struct sockaddr *)&forward_client_addr, &forward_client_addr_len) == -1) {
        perror("getsockname");
        close(listen_sock);
        close(forward_sock);
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Listening on port %s:%d for server\n", inet_ntoa(forward_client_addr.sin_addr), ntohs(forward_client_addr.sin_port));

    // Set up forward address
    memset(&forward_addr, 0, sizeof(forward_addr));
    forward_addr.sin_family = AF_INET;
    struct hostent *host = gethostbyname(forward_host);
    if (host == NULL) {
        perror("gethostbyname");
        close(listen_sock);
        close(forward_sock);
        exit(EXIT_FAILURE);
    }
    forward_addr.sin_addr = *(struct in_addr *)host->h_addr;
    forward_addr.sin_port = htons(forward_port);

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
            ssize_t received = recvfrom(listen_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&last_sender_addr_temp, &last_sender_addr_temp_len);
            if (received < 0) {
                perror("recvfrom");
                continue;
            }

            // Store the last sender address
            if (received >= sizeof(wg_signature) && memcmp(buffer, wg_signature, sizeof(wg_signature)) == 0) {
                fprintf(stderr, "Received WireGuard handshake (non-obfuscated) from %s:%d\n", inet_ntoa(last_sender_addr_temp.sin_addr), ntohs(last_sender_addr_temp.sin_port));
                last_handshake_time = time(NULL);
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
                fprintf(stderr, "Received WireGuard handshake (obfuscated) from %s:%d\n", inet_ntoa(last_sender_addr_temp.sin_addr), ntohs(last_sender_addr_temp.sin_port));
                last_handshake_time = time(NULL);
            }

            // Send the data to the forward port
            sendto(forward_sock, buffer, received, 0, (struct sockaddr *)&forward_addr, sizeof(forward_addr));
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

            int need_to_set_clietn_addr = 0;
            // Check if the response is a WireGuard handshake response
            if (received >= sizeof(wg_signature_resp) && memcmp(buffer, wg_signature_resp, sizeof(wg_signature_resp)) == 0) {
                fprintf(stderr, "Received WireGuard handshake response (non-obfuscated) from %s:%d\n", inet_ntoa(forward_server_addr.sin_addr), ntohs(forward_server_addr.sin_port));
                need_to_set_clietn_addr = 1;
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
                fprintf(stderr, "Received WireGuard handshake response (obfuscated) from %s:%d\n", inet_ntoa(forward_server_addr.sin_addr), ntohs(forward_server_addr.sin_port));
                need_to_set_clietn_addr = 1;
            }

            // Store the last sender address
            if (need_to_set_clietn_addr) {
                if (time(NULL) - last_handshake_time < HANDSHAKE_TIMEOUT) {
                    memcpy(&last_sender_addr, &last_sender_addr_temp, sizeof(last_sender_addr));
                    last_sender_set = 1;
                    fprintf(stderr, "Client address set to %s:%d\n", inet_ntoa(last_sender_addr.sin_addr), ntohs(last_sender_addr.sin_port));
                } else {
                    fprintf(stderr, "Ignoring WireGuard handshake response, handshake timeout\n");
                }
            }

            // Send the response back to the original client
            if (last_sender_set) {
                sendto(listen_sock, buffer, received, 0, (struct sockaddr *)&last_sender_addr, sizeof(last_sender_addr));
            }
        }
    }

    close(listen_sock);
    close(forward_sock);

    fprintf(stderr, "Stopped.\n");

    return 0;
}
