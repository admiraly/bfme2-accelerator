#include "bfme2_crc32_algo.h"
#include <cstdio>
static uint32_t state = 0x12345678;
static uint32_t random32() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
static uint32_t bitwise(const unsigned char* p, unsigned n, uint32_t seed) {
    uint32_t crc = ~seed;
    while (n--) { crc ^= *p++; for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0); }
    return ~crc;
}
int main() {
    uint32_t table[8][256]; bfme2Crc32Tables(table); unsigned char data[2112]; unsigned cases = 0;
    if (bfme2Crc32((const unsigned char*)"123456789", 9, 0, table) != 0xcbf43926) return 1;
    for (unsigned batch = 0; batch < 4; ++batch) {
        for (auto& b : data) b = (unsigned char)random32();
        for (unsigned n = 0; n <= 2048; ++n) for (unsigned align = 0; align < 16; ++align) {
            uint32_t seed = random32();
            if (bfme2Crc32(data + align, n, seed, table) != bitwise(data + align, n, seed)) return 1;
            ++cases;
        }
    }
    if (bfme2Crc32(NULL, 0, 123, table) != 123) return 1;
    printf("IEEE CRC32: %u bitwise oracle cases and standard check vector passed\n", cases);
    return 0;
}
