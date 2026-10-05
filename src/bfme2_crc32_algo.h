#ifndef BFME2_CRC32_ALGO_H
#define BFME2_CRC32_ALGO_H
#include <stdint.h>
#include <string.h>
static void bfme2Crc32Tables(uint32_t table[8][256]) {
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (unsigned bit = 0; bit < 8; ++bit) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        table[0][i] = c;
    }
    for (unsigned slice = 1; slice < 8; ++slice) for (unsigned i = 0; i < 256; ++i) {
        uint32_t c = table[slice-1][i]; table[slice][i] = (c >> 8) ^ table[0][c & 255];
    }
}
static uint32_t bfme2Crc32(const unsigned char* p, uint32_t n, uint32_t seed, const uint32_t t[8][256]) {
    uint32_t crc = ~seed;
    while (n >= 8) {
        uint32_t a, b; memcpy(&a, p, 4); memcpy(&b, p+4, 4); a ^= crc;
        crc = t[7][a & 255] ^ t[6][(a >> 8) & 255] ^ t[5][(a >> 16) & 255] ^ t[4][a >> 24] ^
              t[3][b & 255] ^ t[2][(b >> 8) & 255] ^ t[1][(b >> 16) & 255] ^ t[0][b >> 24];
        p += 8; n -= 8;
    }
    if (n >= 4) {
        uint32_t a; memcpy(&a, p, 4); a ^= crc;
        crc = t[3][a & 255] ^ t[2][(a >> 8) & 255] ^ t[1][(a >> 16) & 255] ^ t[0][a >> 24];
        p += 4; n -= 4;
    }
    while (n--) crc = (crc >> 8) ^ t[0][(crc ^ *p++) & 255];
    return ~crc;
}
#endif
