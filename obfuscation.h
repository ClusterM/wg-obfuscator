#ifndef _OBFUSCATION_H_
#define _OBFUSCATION_H_

#include <stdint.h>

// Current obfuscation version
#define OBFUSCATION_VERSION     1

// Maximum length (in bytes) of a single cached keystream row.
// The keystream depends only on (key, length mod 256), so it is cached in 256
// lazily-grown rows. Packets longer than this limit are still handled correctly:
// the part beyond the limit is computed on the fly. Worst-case cache memory is
// 256 * KEYSTREAM_ROW_MAX bytes. 2048 covers a typical MTU plus masking overhead.
#define KEYSTREAM_ROW_MAX       2048

// WireGuard packet types
#define WG_TYPE_HANDSHAKE       0x01
#define WG_TYPE_HANDSHAKE_RESP  0x02
#define WG_TYPE_COOKIE          0x03
#define WG_TYPE_DATA            0x04

#define WG_TYPE(data) ((uint32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24)))
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/**
 * Checks if the given data is obfuscated.
 *
 * @param data Pointer to the data buffer to check.
 * @return uint8_t Returns a non-zero value if the data is obfuscated, 0 otherwise.
 */
static inline uint8_t is_obfuscated(uint8_t *data) {
    uint32_t packet_type = WG_TYPE(data);
    return !(packet_type >= 1 && packet_type <= 4);
}

/**
 * @brief XORs the data in the given buffer with the key-derived keystream.
 *
 * The keystream is a per-byte CRC8 value derived from the key, the byte position
 * and the packet length; it depends only on (key, length mod 256), so it is
 * cached (see obfuscation.c). The on-the-wire result is byte-for-byte identical
 * to the original bit-by-bit implementation.
 *
 * @param buffer Pointer to the data buffer to be XORed.
 * @param length Length of the data buffer in bytes.
 * @param key Pointer to the key used for XOR operation.
 * @param key_length Length of the key in bytes.
 */
void xor_data(uint8_t *buffer, int length, char *key, int key_length);

/**
 * @brief Encodes the given buffer using the specified key and version.
 *
 * This function applies an encoding algorithm to the input buffer using the provided key and version.
 * WARNING: buffer must be at least 4 bytes long and aligned to 4 bytes.
 *
 * @param buffer                    Pointer to the data buffer to encode.
 * @param length                    Length of the data buffer in bytes.
 * @param key                       Pointer to the key used for encoding.
 * @param key_length                Length of the key in bytes.
 * @param version                   Encoding version to use.
 * @param max_dummy_length_data     Maximum length of dummy data for data packets.
 * @return                          0 on success, or a negative value on error.
 */
static inline int encode(uint8_t *buffer, int length, char *key, int key_length, uint8_t version, int max_dummy_length_data) {
    if (version >= 1) {
        uint32_t packet_type = WG_TYPE(buffer);
        // Add some randomness to the packet
        uint8_t rnd = 1 + (rand() % 255);
        buffer[0] ^= rnd; // Xor the first byte to a random value
        buffer[1] = rnd; // Set the second byte to a random value
        // Add dummy data to the packet
        if (length < MAX_DUMMY_LENGTH_TOTAL) {
            uint16_t dummy_length = 0;
            uint16_t max_dummy_length = MAX_DUMMY_LENGTH_TOTAL - length;
            if (length < MAX_DUMMY_LENGTH_TOTAL) {
                switch (packet_type) {
                    case WG_TYPE_HANDSHAKE:
                    case WG_TYPE_HANDSHAKE_RESP:
                        // length to MAX_DUMMY_LENGTH_HANDSHAKE
                        dummy_length = rand() % MIN(max_dummy_length, MAX_DUMMY_LENGTH_HANDSHAKE);
                        break;
                    case WG_TYPE_COOKIE:
                    case WG_TYPE_DATA:
                        // length to MAX_DUMMY_LENGTH_HANDSHAKE
                        if (max_dummy_length_data) {
                            dummy_length = rand() % MIN(max_dummy_length, max_dummy_length_data);
                        }
                        break;
                    default:
                        //assert(0);
                        break;
                }
            }
            buffer[2] = dummy_length & 0xFF; // Set the dummy length in the packet
            buffer[3] = dummy_length >> 8; // Set the dummy length in
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

/**
 * Decodes the given buffer using the provided key.
 * 
 * WARNING: buffer must be at least 4 bytes long and aligned to 4 bytes.
 *
 * @param buffer        Pointer to the input buffer to decode.
 * @param length        Length of the input buffer.
 * @param key           Pointer to the key used for decoding.
 * @param key_length    Length of the key.
 * @param version_out   Pointer to a variable where the decoded version will be stored.
 * @return              Length of the decoded data (smaller than or equal to the input length).
 */
static inline int decode(uint8_t *buffer, int length, char *key, int key_length, uint8_t *version_out) {
    xor_data(buffer, length, key, key_length);

    if (!is_obfuscated(buffer)) {
        // Looks like an old version
        *version_out = 0;
        return length;
    }

    buffer[0] ^= buffer[1]; // Restore the first byte by XORing it with the second byte
    length -= (uint16_t)(buffer[2] | (buffer[3] << 8)); // Remove dummy data length from the packet
    buffer[1] = buffer[2] = buffer[3] = 0; // Reset the dummy length field to 0
    return length;
}

#endif // _OBFUSCATION_H_