#ifndef _WG_OBFUSCATOR_H_
#define _WG_OBFUSCATOR_H_

#include <stdint.h>

#define VERSION "1.0"
#define GIT_REPO "https://github.com/ClusterM/wg-obfuscator"

#define BUFFER_SIZE 2048
#define HANDSHAKE_TIMEOUT 5
#define MAX_DUMMY_LENGTH 1024

// WireGuard handshake signature
static const uint8_t wg_signature_handshake[] = {0x01, 0x00, 0x00, 0x00};
static const uint8_t wg_signature_handshake_resp[] = {0x02, 0x00, 0x00, 0x00};

// WireGuard handshake lengths (for latest versions)
#define HANDSHAKE_LENGTH 148
#define HANDSHAKE_RESP_LENGTH 92

#endif
