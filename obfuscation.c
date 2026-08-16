#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "wg-obfuscator.h"
#include "obfuscation.h"

static uint8_t crc_a[256]; // step(crc, 0): contribution of the current CRC state
static uint8_t crc_b[256]; // step(0, inbyte): contribution of the input byte
static uint8_t tables_ready = 0;

typedef struct {
    uint8_t *data;  // cached keystream bytes, NULL until first allocation
    int      valid; // number of valid bytes in data
    int      cap;   // allocated capacity of data
    uint8_t  crc;   // CRC state right after the 'valid' bytes
} keystream_row_t;

static keystream_row_t rows[256];

// Single step of the original bit-by-bit CRC8 update, kept only to build the tables.
static uint8_t crc8_step(uint8_t crc, uint8_t inbyte) {
    for (uint8_t j = 0; j < 8; j++) {
        uint8_t mix = (crc ^ inbyte) & 0x01;
        crc >>= 1;
        if (mix) {
            crc ^= 0x8C;
        }
        inbyte >>= 1;
    }
    return crc;
}

static void ensure_tables(void) {
    if (tables_ready) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        crc_a[i] = crc8_step((uint8_t)i, 0);
        crc_b[i] = crc8_step(0, (uint8_t)i);
    }
    tables_ready = 1;
}

// Ensure the given row holds at least 'need' valid keystream bytes (capped at
// KEYSTREAM_ROW_MAX). On allocation failure the row is left as large as it could
// grow; the caller then computes the missing tail on the fly.
static void grow_row(keystream_row_t *row, int cls, int need, const char *key, int key_length) {
    if (need > KEYSTREAM_ROW_MAX) {
        need = KEYSTREAM_ROW_MAX;
    }
    if (row->valid >= need) {
        return;
    }
    if (row->cap < need) {
        int newcap = (need + 255) & ~255; // round up to a 256-byte chunk
        if (newcap > KEYSTREAM_ROW_MAX) {
            newcap = KEYSTREAM_ROW_MAX;
        }
        uint8_t *p = realloc(row->data, newcap);
        if (!p) {
            return; // keep whatever we already have; tail handled by caller
        }
        row->data = p;
        row->cap = newcap;
    }

    uint8_t crc = row->crc;
    int ki = row->valid % key_length;
    for (int i = row->valid; i < need; i++) {
        uint8_t inbyte = (uint8_t)(key[ki] + cls + key_length);
        crc = crc_a[crc] ^ crc_b[inbyte];
        row->data[i] = crc;
        if (++ki == key_length) {
            ki = 0;
        }
    }
    row->valid = need;
    row->crc = crc;
}

// XOR 'n' keystream bytes into dst, 8 bytes at a time. memcpy avoids both
// strict-aliasing violations and unaligned-access assumptions.
static void xor_block(uint8_t *dst, const uint8_t *ks, int n) {
    int i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t a, b;
        memcpy(&a, dst + i, 8);
        memcpy(&b, ks + i, 8);
        a ^= b;
        memcpy(dst + i, &a, 8);
    }
    for (; i < n; i++) {
        dst[i] ^= ks[i];
    }
}

void xor_data(uint8_t *buffer, int length, char *key, int key_length) {
    if (length <= 0) {
        return;
    }
    ensure_tables();

    int cls = length & 0xFF;
    keystream_row_t *row = &rows[cls];

    int cached = (length <= KEYSTREAM_ROW_MAX) ? length : KEYSTREAM_ROW_MAX;
    if (row->valid < cached) {
        grow_row(row, cls, cached, key, key_length);
    }

    int n = (length < row->valid) ? length : row->valid;
    if (n > 0) {
        xor_block(buffer, row->data, n);
    }

    // Tail beyond the cache limit (or beyond what we could allocate): compute on
    // the fly, continuing the CRC chain from the last cached state.
    if (n < length) {
        uint8_t crc = row->crc;
        int ki = n % key_length;
        for (int i = n; i < length; i++) {
            uint8_t inbyte = (uint8_t)(key[ki] + cls + key_length);
            crc = crc_a[crc] ^ crc_b[inbyte];
            buffer[i] ^= crc;
            if (++ki == key_length) {
                ki = 0;
            }
        }
    }
}
