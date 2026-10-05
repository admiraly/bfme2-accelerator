#ifndef BFME2_CRC_ALGO_H
#define BFME2_CRC_ALGO_H
#include <stdint.h>
// Native packet/RNG hash, NOT IEEE CRC32 or CRC32C. Each byte is added AFTER
// the rotate; regrouping additions across rotations would change carries.
static inline uint32_t bfme2HashByte(uint32_t crc, unsigned char byte) {
    return ((crc << 1) | (crc >> 31)) + byte;
}
static uint32_t bfme2PacketCrc(const unsigned char* p, uint32_t n, uint32_t crc) {
    if (!p) return crc;
    while (n >= 8) {
        crc = bfme2HashByte(crc, p[0]); crc = bfme2HashByte(crc, p[1]);
        crc = bfme2HashByte(crc, p[2]); crc = bfme2HashByte(crc, p[3]);
        crc = bfme2HashByte(crc, p[4]); crc = bfme2HashByte(crc, p[5]);
        crc = bfme2HashByte(crc, p[6]); crc = bfme2HashByte(crc, p[7]);
        p += 8; n -= 8;
    }
    while (n--) crc = bfme2HashByte(crc, *p++);
    return crc;
}
#endif
