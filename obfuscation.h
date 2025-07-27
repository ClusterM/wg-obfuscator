#ifndef _OBFUSCATION_H_
#define _OBFUSCATION_H_

#include <stdint.h>

// Current obfuscation version
#define OBFUSCATION_VERSION     1

// WireGuard packet types
#define WG_TYPE_HANDSHAKE       0x01
#define WG_TYPE_HANDSHAKE_RESP  0x02
#define WG_TYPE_COOKIE          0x03
#define WG_TYPE_DATA            0x04

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WG_TYPE(data) __builtin_bswap32(*((uint32_t*)(data)))
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define WG_TYPE(data) (*((uint32_t*)(data)))
#else
#error "Cannot determine endianness!"
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
 * WARNING: buffer must be at least 4 bytes long and aligned to 4 bytes.
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
        uint32_t packet_type = WG_TYPE(buffer);
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
            if (length + dummy_length > MAX_DUMMY_LENGTH_TOTAL) {
                dummy_length = MAX_DUMMY_LENGTH_TOTAL - length;
            }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            *((uint16_t*)(buffer+2)) = __builtin_bswap16(dummy_length); // Set the dummy length in the packet
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            *((uint16_t*)(buffer+2)) = dummy_length; // Set the dummy length in the packet
#else
    #error "Cannot determine endianness!"
#endif
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
    buffer[1] = 0; // Set the second byte to 0
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    length -= __builtin_bswap16(*((uint16_t*)(buffer+2))); // Remove dummy data length from the packet
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    length -= *((uint16_t*)(buffer+2)); // Remove dummy data length from the packet
#else
    #error "Cannot determine endianness!"
#endif
    *((uint16_t*)(buffer+2)) = 0; // Reset the dummy length field to 0
    return length;
}

#endif // _OBFUSCATION_H_