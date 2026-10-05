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
        // A sufficient conservative guard for native addition overflow within
        // the next eight steps. Every dangerous rotated value has its high
        // 16 bits set; the shared original bits are 15..23. Handle those rare
        // inputs with the exact scalar recurrence rather than regrouping them.
        if ((crc & 0x00FF8000u) == 0x00FF8000u) {
            crc = bfme2HashByte(crc, p[0]); crc = bfme2HashByte(crc, p[1]);
            crc = bfme2HashByte(crc, p[2]); crc = bfme2HashByte(crc, p[3]);
            crc = bfme2HashByte(crc, p[4]); crc = bfme2HashByte(crc, p[5]);
            crc = bfme2HashByte(crc, p[6]); crc = bfme2HashByte(crc, p[7]);
        } else {
            // With no native wrap, rotations distribute in one's-complement
            // arithmetic. Sum <=65025; one end-around carry suffices. Preserve
            // 0xFFFFFFFF as distinct from zero, as the stock recurrence does.
            uint32_t sum = ((uint32_t)p[0] * 8 + p[1] * 4 + p[2] * 2 + p[3]) * 16 +
                           (uint32_t)p[4] * 8 + p[5] * 4 + p[6] * 2 + p[7];
            uint32_t rotated = (crc << 8) | (crc >> 24);
            uint32_t value = rotated + sum;
            crc = value + (value < rotated);
        }
        p += 8; n -= 8;
    }
    while (n--) crc = bfme2HashByte(crc, *p++);
    return crc;
}
#endif
