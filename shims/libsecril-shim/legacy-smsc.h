#pragma once

#include <stddef.h>

static inline int smscHexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* XMM RILs return the complete SMSC PDU, while Android's SMS dispatcher
 * expects a dialable address and performs its own BCD encoding. */
static inline bool decodeLegacySmscPdu(const void *data, size_t size,
        char *address, size_t capacity) {
    if (data == NULL || address == NULL || capacity == 0) return false;
    const char *pdu = static_cast<const char *>(data);
    if (size > 0 && pdu[size - 1] == '\0') --size;
    if (size < 6 || size > 24 || (size & 1)) return false;
    unsigned char bytes[12];
    for (size_t i = 0; i < size / 2; ++i) {
        int hi = smscHexDigit(pdu[i * 2]);
        int lo = smscHexDigit(pdu[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes[i] = (hi << 4) | lo;
    }
    if (bytes[0] != size / 2 - 1 || (bytes[1] != 0x91 && bytes[1] != 0x81))
        return false;
    size_t used = 0;
    if (bytes[1] == 0x91) {
        if (capacity < 2) return false;
        address[used++] = '+';
    }
    for (size_t i = 2; i < size / 2; ++i) {
        unsigned int low = bytes[i] & 0xf;
        unsigned int high = bytes[i] >> 4;
        if (low > 9 || used + 1 >= capacity) return false;
        address[used++] = '0' + low;
        if (high == 0xf && i == size / 2 - 1) break;
        if (high > 9 || used + 1 >= capacity) return false;
        address[used++] = '0' + high;
    }
    address[used] = '\0';
    return true;
}
