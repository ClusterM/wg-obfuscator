/**
 * WireGuard Packet Emulator
 *
 * This tool emulates WireGuard client/server behavior for testing the obfuscator.
 * It can run in two modes:
 *   - CLIENT: Sends WireGuard handshake and data packets to obfuscator
 *   - SERVER: Receives packets and responds (echo server)
 *
 * Usage:
 *   ./test_wg_emulator client <dest_host> <dest_port>
 *   ./test_wg_emulator server <listen_port>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define WG_TYPE_HANDSHAKE       0x01
#define WG_TYPE_HANDSHAKE_RESP  0x02
#define WG_TYPE_COOKIE          0x03
#define WG_TYPE_DATA            0x04

#define BUFFER_SIZE 65535
#define HANDSHAKE_SIZE 148
#define DATA_HEADER_SIZE 16

static volatile int running = 1;

void signal_handler(int signum) {
    running = 0;
}

// Create a fake WireGuard handshake initiation packet
void create_handshake_initiation(uint8_t *buffer, int *length) {
    memset(buffer, 0, HANDSHAKE_SIZE);

    // Type (4 bytes, little-endian)
    buffer[0] = WG_TYPE_HANDSHAKE;
    buffer[1] = 0x00;
    buffer[2] = 0x00;
    buffer[3] = 0x00;

    // Sender index (4 bytes)
    uint32_t sender_idx = rand();
    memcpy(buffer + 4, &sender_idx, 4);

    // Unencrypted ephemeral (32 bytes) - random
    for (int i = 8; i < 40; i++) {
        buffer[i] = rand() % 256;
    }

    // Encrypted static (48 bytes) - random
    for (int i = 40; i < 88; i++) {
        buffer[i] = rand() % 256;
    }

    // Encrypted timestamp (28 bytes) - random
    for (int i = 88; i < 116; i++) {
        buffer[i] = rand() % 256;
    }

    // MAC1 (16 bytes) - random
    for (int i = 116; i < 132; i++) {
        buffer[i] = rand() % 256;
    }

    // MAC2 (16 bytes) - random
    for (int i = 132; i < 148; i++) {
        buffer[i] = rand() % 256;
    }

    *length = HANDSHAKE_SIZE;
}

// Create a fake WireGuard handshake response packet
void create_handshake_response(uint8_t *buffer, int *length, uint32_t sender_idx) {
    memset(buffer, 0, 92);

    // Type
    buffer[0] = WG_TYPE_HANDSHAKE_RESP;
    buffer[1] = 0x00;
    buffer[2] = 0x00;
    buffer[3] = 0x00;

    // Sender index
    uint32_t resp_sender_idx = rand();
    memcpy(buffer + 4, &resp_sender_idx, 4);

    // Receiver index (echo back sender index)
    memcpy(buffer + 8, &sender_idx, 4);

    // Unencrypted ephemeral (32 bytes)
    for (int i = 12; i < 44; i++) {
        buffer[i] = rand() % 256;
    }

    // Encrypted empty (16 bytes)
    for (int i = 44; i < 60; i++) {
        buffer[i] = rand() % 256;
    }

    // MAC1 (16 bytes)
    for (int i = 60; i < 76; i++) {
        buffer[i] = rand() % 256;
    }

    // MAC2 (16 bytes)
    for (int i = 76; i < 92; i++) {
        buffer[i] = rand() % 256;
    }

    *length = 92;
}

// Create a fake WireGuard data packet
void create_data_packet(uint8_t *buffer, int *length, uint32_t receiver_idx,
                       uint64_t counter, const char *payload, int payload_len) {
    int total_len = DATA_HEADER_SIZE + payload_len;
    memset(buffer, 0, total_len);

    // Type
    buffer[0] = WG_TYPE_DATA;
    buffer[1] = 0x00;
    buffer[2] = 0x00;
    buffer[3] = 0x00;

    // Receiver index
    memcpy(buffer + 4, &receiver_idx, 4);

    // Counter
    memcpy(buffer + 8, &counter, 8);

    // Encrypted payload (just copy plaintext for testing)
    if (payload && payload_len > 0) {
        memcpy(buffer + DATA_HEADER_SIZE, payload, payload_len);
    }

    *length = total_len;
}

// CLIENT MODE: Send handshake and data packets
int run_client(const char *dest_host, int dest_port) {
    int sockfd;
    struct sockaddr_in dest_addr;
    uint8_t send_buffer[BUFFER_SIZE];
    uint8_t recv_buffer[BUFFER_SIZE];
    int len;

    printf("[CLIENT] Starting WireGuard emulator client\n");
    printf("[CLIENT] Target: %s:%d\n", dest_host, dest_port);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Setup destination address
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    if (inet_pton(AF_INET, dest_host, &dest_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    printf("\n=== Test 1: Handshake Initiation ===\n");

    // Send handshake initiation
    create_handshake_initiation(send_buffer, &len);
    uint32_t sender_idx;
    memcpy(&sender_idx, send_buffer + 4, 4);

    printf("[CLIENT] Sending handshake initiation (%d bytes, sender_idx=%u)...\n",
           len, sender_idx);

    ssize_t sent = sendto(sockfd, send_buffer, len, 0,
                         (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (sent < 0) {
        perror("sendto");
        close(sockfd);
        return 1;
    }
    printf("[CLIENT] Sent %zd bytes\n", sent);

    // Wait for handshake response
    printf("[CLIENT] Waiting for handshake response...\n");
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(sockfd, recv_buffer, BUFFER_SIZE, 0,
                               (struct sockaddr*)&from_addr, &from_len);

    if (received > 0) {
        printf("[CLIENT] Received %zd bytes from %s:%d\n",
               received, inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port));

        // Check if it's a handshake response
        uint32_t packet_type = recv_buffer[0] | (recv_buffer[1] << 8) |
                              (recv_buffer[2] << 16) | (recv_buffer[3] << 24);
        if (packet_type == WG_TYPE_HANDSHAKE_RESP) {
            printf("[CLIENT] ✓ Received handshake response!\n");

            // Extract receiver index (should match our sender_idx)
            uint32_t receiver_idx;
            memcpy(&receiver_idx, recv_buffer + 8, 4);
            printf("[CLIENT] Receiver index in response: %u (expected: %u)\n",
                   receiver_idx, sender_idx);
        } else {
            printf("[CLIENT] ✗ Unexpected packet type: 0x%08x\n", packet_type);
        }
    } else if (received < 0 && errno == EAGAIN) {
        printf("[CLIENT] ⚠ Timeout waiting for response\n");
    } else {
        perror("recvfrom");
    }

    printf("\n=== Test 2: Data Packets ===\n");

    // Send some data packets
    for (int i = 0; i < 5; i++) {
        char payload[128];
        snprintf(payload, sizeof(payload), "Test packet #%d from emulator", i + 1);

        create_data_packet(send_buffer, &len, sender_idx, i, payload, strlen(payload));

        printf("[CLIENT] Sending data packet %d (%d bytes)...\n", i + 1, len);
        sent = sendto(sockfd, send_buffer, len, 0,
                     (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        if (sent < 0) {
            perror("sendto");
        } else {
            printf("[CLIENT] Sent %zd bytes\n", sent);
        }

        // Try to receive response
        received = recvfrom(sockfd, recv_buffer, BUFFER_SIZE, 0,
                          (struct sockaddr*)&from_addr, &from_len);
        if (received > 0) {
            printf("[CLIENT] Received %zd bytes (echo)\n", received);
        }

        usleep(100000); // 100ms delay between packets
    }

    printf("\n[CLIENT] Test complete\n");
    close(sockfd);
    return 0;
}

// SERVER MODE: Receive and echo packets
int run_server(int listen_port) {
    int sockfd;
    struct sockaddr_in listen_addr, client_addr;
    uint8_t buffer[BUFFER_SIZE];
    socklen_t client_len;

    printf("[SERVER] Starting WireGuard emulator server\n");
    printf("[SERVER] Listening on port %d\n", listen_port);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Bind to listen port
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(listen_port);

    if (bind(sockfd, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("[SERVER] Listening for packets...\n\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int packet_count = 0;

    while (running) {
        client_len = sizeof(client_addr);
        ssize_t received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                                   (struct sockaddr*)&client_addr, &client_len);

        if (received < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }

        packet_count++;
        printf("[SERVER] Packet #%d: Received %zd bytes from %s:%d\n",
               packet_count, received,
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        if (received >= 4) {
            uint32_t packet_type = buffer[0] | (buffer[1] << 8) |
                                  (buffer[2] << 16) | (buffer[3] << 24);

            switch (packet_type) {
                case WG_TYPE_HANDSHAKE:
                    printf("[SERVER] → Handshake Initiation detected\n");

                    // Extract sender index
                    uint32_t sender_idx;
                    memcpy(&sender_idx, buffer + 4, 4);
                    printf("[SERVER] → Sender index: %u\n", sender_idx);

                    // Send handshake response
                    int resp_len;
                    create_handshake_response(buffer, &resp_len, sender_idx);
                    ssize_t sent = sendto(sockfd, buffer, resp_len, 0,
                                        (struct sockaddr*)&client_addr, client_len);
                    if (sent > 0) {
                        printf("[SERVER] ← Sent handshake response (%zd bytes)\n", sent);
                    }
                    break;

                case WG_TYPE_HANDSHAKE_RESP:
                    printf("[SERVER] → Handshake Response\n");
                    break;

                case WG_TYPE_COOKIE:
                    printf("[SERVER] → Cookie Reply\n");
                    break;

                case WG_TYPE_DATA:
                    printf("[SERVER] → Data Packet\n");
                    // Echo back
                    ssize_t echoed = sendto(sockfd, buffer, received, 0,
                                          (struct sockaddr*)&client_addr, client_len);
                    if (echoed > 0) {
                        printf("[SERVER] ← Echoed %zd bytes\n", echoed);
                    }
                    break;

                default:
                    printf("[SERVER] → Unknown packet type: 0x%08x\n", packet_type);
                    break;
            }
        }

        printf("\n");
    }

    printf("[SERVER] Shutting down (received %d packets)\n", packet_count);
    close(sockfd);
    return 0;
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    if (argc < 3) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  Client mode: %s client <dest_host> <dest_port>\n", argv[0]);
        fprintf(stderr, "  Server mode: %s server <listen_port>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "client") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Client mode requires: <dest_host> <dest_port>\n");
            return 1;
        }
        return run_client(argv[2], atoi(argv[3]));
    }
    else if (strcmp(argv[1], "server") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Server mode requires: <listen_port>\n");
            return 1;
        }
        return run_server(atoi(argv[2]));
    }
    else {
        fprintf(stderr, "Unknown mode: %s\n", argv[1]);
        fprintf(stderr, "Use 'client' or 'server'\n");
        return 1;
    }

    return 0;
}
