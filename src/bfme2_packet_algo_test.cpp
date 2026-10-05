#include "bfme2_packet_algo.h"
#include <cstdio>
#include <climits>
static uint32_t state = 0x12345678;
static unsigned random32() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
// Byte-wise oracle, independent of SIMD and host word loads.
static void reference(unsigned char* p, int n, bool decode) {
    uint32_t key = 0x38D9B7D4u;
    for (int i = 0; i < n / 4; ++i, p += 4, key -= 0x7F39C50Eu) {
        unsigned char b[4]; memcpy(b, p, 4);
        for (unsigned j = 0; j < 4; ++j) {
            unsigned shift = 8 * (decode ? j : 3 - j);
            p[j] = b[3-j] ^ (unsigned char)(key >> shift);
        }
    }
}
int main() {
    unsigned char original[1152], actual[1152], expected[1152];
    unsigned cases = 0;
    for (unsigned batch = 0; batch < 16; ++batch) {
        for (unsigned i = 0; i < sizeof(original); ++i) original[i] = (unsigned char)random32();
        for (int n = -7; n <= 1100; ++n) for (unsigned offset = 0; offset < 32; ++offset) {
            for (unsigned decode = 0; decode < 2; ++decode) {
                memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
                reference(expected + offset, n, !!decode);
                if (decode) bfme2PacketTransform<true>(actual + offset, n);
                else bfme2PacketTransform<false>(actual + offset, n);
                if (memcmp(actual, expected, sizeof(actual))) return 1;
                ++cases;
            }
        }
    }
    bfme2PacketTransform<false>(NULL, INT_MIN);
    bfme2PacketTransform<true>(NULL, 0);
    printf("Packet SIMD: %u byte-wise oracle cases and null no-work inputs passed\n", cases);
    return 0;
}
