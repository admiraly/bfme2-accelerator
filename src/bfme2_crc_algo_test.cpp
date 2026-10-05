#include "bfme2_crc_algo.h"
#include <cstdio>
#include <cstring>
static uint32_t randomState = 0x12345678;
static uint32_t random32() { randomState ^= randomState << 13; randomState ^= randomState >> 17; randomState ^= randomState << 5; return randomState; }
static uint32_t reference(const unsigned char* p, uint32_t n, uint32_t crc) {
    if (!p) return crc;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t carry = crc / 0x80000000u;
        crc = (uint32_t)((uint64_t)crc * 2 + p[i] + carry);
    }
    return crc;
}
int main() {
    unsigned char input[2112]; unsigned count = 0;
    const uint32_t seeds[] = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0xffffff80, 0x12345678};
    for (uint32_t seed : seeds) for (unsigned byte = 0; byte < 256; ++byte) {
        input[0] = (unsigned char)byte;
        if (reference(input, 1, seed) != bfme2PacketCrc(input, 1, seed)) return 1;
        ++count;
    }
    for (unsigned batch = 0; batch < 4; ++batch) {
        for (unsigned i = 0; i < sizeof(input); ++i) input[i] = (unsigned char)random32();
        for (unsigned n = 0; n <= 2048; ++n) for (unsigned align = 0; align < 32; ++align) {
            uint32_t seed = random32();
            if (reference(input + align, n, seed) != bfme2PacketCrc(input + align, n, seed)) return 1;
            ++count;
        }
    }
    if (bfme2PacketCrc(NULL, 0xffffffffu, 0x12345678) != 0x12345678) return 1;
    printf("BFME II rotate/add hash: %u independent oracle cases passed\n", count);
    return 0;
}
