#ifndef BFME2_PACKET_ALGO_H
#define BFME2_PACKET_ALGO_H
#include <stdint.h>
#include <string.h>
#include <emmintrin.h>
// Independently derived from the pinned 1.06 machine code. Trailing bytes
// remain untouched; unsigned arithmetic deliberately wraps modulo 2^32.
static inline __m128i bfme2SwapWords(__m128i x) {
    x = _mm_or_si128(_mm_slli_epi16(x, 8), _mm_srli_epi16(x, 8));
    x = _mm_shufflelo_epi16(x, _MM_SHUFFLE(2, 3, 0, 1));
    return _mm_shufflehi_epi16(x, _MM_SHUFFLE(2, 3, 0, 1));
}
template<bool Decode> static void bfme2PacketTransform(void* buffer, int length) {
    if (length < 4) return;
    unsigned words = (unsigned)length / 4;
    unsigned char* p = (unsigned char*)buffer;
    const uint32_t step = 0x7F39C50Eu;
    uint32_t key = 0x38D9B7D4u;
    if (words >= 4) {
        __m128i keys = _mm_set_epi32((int)(key - 3u * step), (int)(key - 2u * step),
                                    (int)(key - step), (int)key);
        const __m128i advance = _mm_set1_epi32((int)(4u * step));
        do {
            __m128i value = _mm_loadu_si128((const __m128i*)p);
            value = Decode ? _mm_xor_si128(bfme2SwapWords(value), keys)
                           : bfme2SwapWords(_mm_xor_si128(value, keys));
            _mm_storeu_si128((__m128i*)p, value);
            keys = _mm_sub_epi32(keys, advance);
            key -= 4u * step; words -= 4; p += 16;
        } while (words >= 4);
    }
    while (words--) {
        uint32_t x; memcpy(&x, p, 4);
        if (!Decode) x ^= key;
        x = (x << 24) | ((x & 0xFF00u) << 8) | ((x >> 8) & 0xFF00u) | (x >> 24);
        if (Decode) x ^= key;
        memcpy(p, &x, 4); p += 4; key -= step;
    }
}
#endif
