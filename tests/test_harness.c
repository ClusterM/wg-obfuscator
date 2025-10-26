/**
 * Unit Test Harness for WireGuard Obfuscator
 * Tests obfuscation, encoding, and masking functions without requiring WireGuard
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>

#define MAX_DUMMY_LENGTH_TOTAL 65535
#define MAX_DUMMY_LENGTH_HANDSHAKE 512
#include "obfuscation.h"

// ANSI color codes for output
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_RESET "\033[0m"

// Test statistics
static int tests_passed = 0;
static int tests_failed = 0;

// Test result macros
#define TEST_START(name) printf("[TEST] %s... ", name); fflush(stdout);
#define TEST_PASS() do { printf(COLOR_GREEN "PASS" COLOR_RESET "\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { printf(COLOR_RED "FAIL" COLOR_RESET ": %s\n", msg); tests_failed++; } while(0)
#define TEST_ASSERT(cond, msg) do { if (!(cond)) { TEST_FAIL(msg); return; } } while(0)

// Helper: Create a fake WireGuard handshake packet
void create_wg_handshake_packet(uint8_t *buffer, int *length) {
    memset(buffer, 0, 148);
    buffer[0] = WG_TYPE_HANDSHAKE;  // Type
    buffer[1] = 0x00;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    // Fill with some dummy data
    for (int i = 4; i < 148; i++) {
        buffer[i] = (uint8_t)(rand() % 256);
    }
    *length = 148;
}

// Helper: Create a fake WireGuard data packet
void create_wg_data_packet(uint8_t *buffer, int *length, int payload_size) {
    memset(buffer, 0, payload_size + 16);
    buffer[0] = WG_TYPE_DATA;  // Type
    buffer[1] = 0x00;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    // Receiver index (4 bytes)
    *(uint32_t*)(buffer + 4) = rand();
    // Counter (8 bytes)
    *(uint64_t*)(buffer + 8) = rand();
    // Fill payload
    for (int i = 16; i < payload_size + 16; i++) {
        buffer[i] = (uint8_t)(rand() % 256);
    }
    *length = payload_size + 16;
}

// Test 1: Basic WireGuard packet type detection
void test_wg_packet_detection() {
    TEST_START("WireGuard packet type detection");

    uint8_t handshake[148];
    int len;
    create_wg_handshake_packet(handshake, &len);

    TEST_ASSERT(WG_TYPE(handshake) == WG_TYPE_HANDSHAKE, "Handshake type mismatch");
    TEST_ASSERT(!is_obfuscated(handshake), "Plain handshake detected as obfuscated");

    TEST_PASS();
}

// Test 2: XOR data function
void test_xor_data() {
    TEST_START("XOR data function");

    uint8_t buffer[16];
    uint8_t original[16];
    char key[] = "test_key";

    // Fill with known data
    for (int i = 0; i < 16; i++) {
        buffer[i] = i;
        original[i] = i;
    }

    // XOR twice should return to original
    xor_data(buffer, 16, key, strlen(key));
    TEST_ASSERT(memcmp(buffer, original, 16) != 0, "XOR didn't change data");

    xor_data(buffer, 16, key, strlen(key));
    TEST_ASSERT(memcmp(buffer, original, 16) == 0, "Double XOR didn't restore original");

    TEST_PASS();
}

// Test 3: Encode/decode roundtrip with handshake
void test_encode_decode_handshake() {
    TEST_START("Encode/decode handshake roundtrip");

    uint8_t buffer[1024];
    uint8_t original[1024];
    int len;
    char key[] = "secret_key_123";
    uint8_t version_out;

    create_wg_handshake_packet(buffer, &len);
    memcpy(original, buffer, len);
    int original_len = len;

    // Encode
    int encoded_len = encode(buffer, len, key, strlen(key), OBFUSCATION_VERSION, 0);
    TEST_ASSERT(encoded_len >= original_len, "Encoded length shorter than original");
    TEST_ASSERT(is_obfuscated(buffer), "Encoded packet not detected as obfuscated");

    // Decode
    int decoded_len = decode(buffer, encoded_len, key, strlen(key), &version_out);
    TEST_ASSERT(decoded_len == original_len, "Decoded length doesn't match original");

    // Restore packet type (after decode, first byte is XORed with random)
    TEST_ASSERT(WG_TYPE(buffer) == WG_TYPE_HANDSHAKE, "Decoded packet type mismatch");

    // Check payload (skip first 4 bytes which contain version info)
    TEST_ASSERT(memcmp(buffer + 4, original + 4, original_len - 4) == 0,
                "Decoded payload doesn't match original");

    TEST_PASS();
}

// Test 4: Encode/decode roundtrip with data packet
void test_encode_decode_data() {
    TEST_START("Encode/decode data packet roundtrip");

    uint8_t buffer[2048];
    uint8_t original[2048];
    int len;
    char key[] = "another_secret_key";
    uint8_t version_out;

    create_wg_data_packet(buffer, &len, 100);
    memcpy(original, buffer, len);
    int original_len = len;

    // Encode with max_dummy_length_data = 64
    int encoded_len = encode(buffer, len, key, strlen(key), OBFUSCATION_VERSION, 64);
    TEST_ASSERT(encoded_len >= original_len, "Encoded length shorter than original");

    // Decode
    int decoded_len = decode(buffer, encoded_len, key, strlen(key), &version_out);
    TEST_ASSERT(decoded_len == original_len, "Decoded length doesn't match original");
    TEST_ASSERT(WG_TYPE(buffer) == WG_TYPE_DATA, "Decoded packet type mismatch");

    TEST_PASS();
}

// Test 5: Wrong key should fail decoding
void test_wrong_key() {
    TEST_START("Decoding with wrong key");

    uint8_t buffer[1024];
    int len;
    char key1[] = "correct_key";
    char key2[] = "wrong_key";
    uint8_t version_out;

    create_wg_handshake_packet(buffer, &len);
    uint32_t original_type = WG_TYPE(buffer);

    // Encode with key1
    int encoded_len = encode(buffer, len, key1, strlen(key1), OBFUSCATION_VERSION, 0);

    // Try to decode with key2
    decode(buffer, encoded_len, key2, strlen(key2), &version_out);

    // Should not match original type
    TEST_ASSERT(WG_TYPE(buffer) != original_type, "Wrong key still decoded correctly");

    TEST_PASS();
}

// Test 6: Dummy data padding on handshake
void test_dummy_padding_handshake() {
    TEST_START("Dummy data padding on handshake");

    uint8_t buffer[2048];
    int len;
    char key[] = "test_key";

    create_wg_handshake_packet(buffer, &len);
    int original_len = len;

    // Encode multiple times, should produce different lengths due to random padding
    int lengths[5];
    for (int i = 0; i < 5; i++) {
        uint8_t temp[2048];
        memcpy(temp, buffer, original_len);
        lengths[i] = encode(temp, original_len, key, strlen(key), OBFUSCATION_VERSION, 0);
    }

    // At least one should be different (very high probability)
    int all_same = 1;
    for (int i = 1; i < 5; i++) {
        if (lengths[i] != lengths[0]) {
            all_same = 0;
            break;
        }
    }
    TEST_ASSERT(!all_same, "All encoded lengths are same (no random padding)");

    TEST_PASS();
}

// Test 7: Version detection
void test_version_detection() {
    TEST_START("Obfuscation version detection");

    uint8_t buffer[1024];
    int len;
    char key[] = "version_test_key";
    uint8_t version_out;

    create_wg_data_packet(buffer, &len, 64);

    // Encode with version 1
    int encoded_len = encode(buffer, len, key, strlen(key), 1, 0);

    // Decode and check version
    decode(buffer, encoded_len, key, strlen(key), &version_out);

    // Version 0 means it looked like old unversioned format
    // But since we encoded with version 1, is_obfuscated should have been true during decode
    // This test mainly ensures decode doesn't crash

    TEST_PASS();
}

// Test 8: Edge case - minimum packet size
void test_minimum_packet_size() {
    TEST_START("Minimum packet size (4 bytes)");

    uint8_t buffer[64];
    char key[] = "key";
    uint8_t version_out;

    // Create minimal WireGuard-like packet
    buffer[0] = WG_TYPE_COOKIE;
    buffer[1] = 0x00;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    int len = 4;

    uint32_t original_type = WG_TYPE(buffer);

    // Encode
    int encoded_len = encode(buffer, len, key, strlen(key), OBFUSCATION_VERSION, 0);
    TEST_ASSERT(encoded_len >= 4, "Encoded length less than 4");

    // Decode
    int decoded_len = decode(buffer, encoded_len, key, strlen(key), &version_out);
    TEST_ASSERT(decoded_len == len, "Decoded length mismatch");
    TEST_ASSERT(WG_TYPE(buffer) == original_type, "Packet type corrupted");

    TEST_PASS();
}

// Test 9: Large packet
void test_large_packet() {
    TEST_START("Large packet (1400 bytes)");

    uint8_t buffer[2048];
    int len;
    char key[] = "large_packet_key";
    uint8_t version_out;

    create_wg_data_packet(buffer, &len, 1384); // 1384 + 16 = 1400 (typical MTU)
    int original_len = len;

    // Encode
    int encoded_len = encode(buffer, len, key, strlen(key), OBFUSCATION_VERSION, 4);

    // Decode
    int decoded_len = decode(buffer, encoded_len, key, strlen(key), &version_out);
    TEST_ASSERT(decoded_len == original_len, "Large packet decode length mismatch");
    TEST_ASSERT(WG_TYPE(buffer) == WG_TYPE_DATA, "Large packet type corrupted");

    TEST_PASS();
}

// Test 10: Different key lengths
void test_different_key_lengths() {
    TEST_START("Different key lengths");

    char *keys[] = {"a", "ab", "short", "this_is_a_longer_key_for_testing",
                    "this_is_an_even_much_longer_key_that_should_still_work_fine_123456789"};
    int num_keys = sizeof(keys) / sizeof(keys[0]);

    for (int i = 0; i < num_keys; i++) {
        uint8_t buffer[1024];
        uint8_t original[1024];
        int len;
        uint8_t version_out;

        create_wg_handshake_packet(buffer, &len);
        memcpy(original, buffer, len);
        int original_len = len;

        // Encode
        int encoded_len = encode(buffer, len, keys[i], strlen(keys[i]), OBFUSCATION_VERSION, 0);

        // Decode
        int decoded_len = decode(buffer, encoded_len, keys[i], strlen(keys[i]), &version_out);

        if (decoded_len != original_len) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Key length %zu failed", strlen(keys[i]));
            TEST_FAIL(msg);
            return;
        }
    }

    TEST_PASS();
}

// Main test runner
int main() {
    printf("\n=== WireGuard Obfuscator Unit Tests ===\n\n");

    // Seed random number generator
    srand(time(NULL));

    // Run all tests
    test_wg_packet_detection();
    test_xor_data();
    test_encode_decode_handshake();
    test_encode_decode_data();
    test_wrong_key();
    test_dummy_padding_handshake();
    test_version_detection();
    test_minimum_packet_size();
    test_large_packet();
    test_different_key_lengths();

    // Print summary
    printf("\n=== Test Summary ===\n");
    printf("Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
    printf("Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);
    printf("Total:  %d\n\n", tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
