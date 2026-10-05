#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
static void logf(const char*, ...) {}
#include "aotr_fastcrt.inc"
// Previous accelerator implementation, retained only as the timing baseline.
static void* __cdecl previousMemmove(void* dst, const void* src, size_t n) {
    BYTE* d = (BYTE*)dst; const BYTE* s = (const BYTE*)src;
    if (!crtOverlap(d, s, n)) { crtCopyFwd(d, s, n); return dst; }
    if (d == s) return dst;
    if (d < s) { __movsb(d, s, n); return dst; }
    size_t i = n;
    while (i >= 32) {
        i -= 32;
        __m128i a = _mm_loadu_si128((const __m128i*)(s + i)), b = _mm_loadu_si128((const __m128i*)(s + i + 16));
        _mm_storeu_si128((__m128i*)(d + i + 16), b); _mm_storeu_si128((__m128i*)(d + i), a);
    }
    while (i--) d[i] = s[i];
    return dst;
}
static unsigned rng = 0x12345678;
static unsigned random32() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static volatile unsigned sink;
static int failed(unsigned n, unsigned shift, unsigned direction) {
    printf("memmove mismatch n=%u shift=%u direction=%u\n", n, shift, direction); return 1;
}
int main(int argc, char** argv) {
    HMODULE crt = LoadLibraryA(argc > 1 ? argv[1] : "msvcr71.dll");
    if (!crt || !crtPrepare(crt)) return 2;
    const unsigned total = 65536 + 4096 + 64;
    std::vector<BYTE> original(total), expected(total), actual(total);
    for (auto& b : original) b = (BYTE)random32();
    unsigned cases = 0;
    for (unsigned n = 0; n <= 1024; ++n) for (unsigned shift = 1; shift <= 32; ++shift)
        for (unsigned align = 0; align < 4; ++align) for (unsigned direction = 0; direction < 2; ++direction) {
            unsigned src = 16 + align + (direction ? 0 : shift);
            unsigned dst = 16 + align + (direction ? shift : 0);
            memcpy(expected.data(), original.data(), 1100); memcpy(actual.data(), original.data(), 1100);
            void* result = fastMemmove(actual.data() + dst, actual.data() + src, n);
            o_crtMemmove(expected.data() + dst, expected.data() + src, n);
            if (result != actual.data() + dst || memcmp(actual.data(), expected.data(), 1100)) return failed(n, shift, direction);
            ++cases;
        }
    for (unsigned n : {2047u, 2048u, 4095u, 4096u, 16384u, 65536u})
        for (unsigned shift : {1u, 8u, 15u, 16u, 17u, 31u, 32u, 63u, 64u, 127u, 256u, 1023u, 4096u})
            for (unsigned direction = 0; direction < 2; ++direction) {
                expected = original; actual = original;
                unsigned src = 16 + (direction ? 0 : shift), dst = 16 + (direction ? shift : 0);
                fastMemmove(actual.data() + dst, actual.data() + src, n);
                o_crtMemmove(expected.data() + dst, expected.data() + src, n);
                if (actual != expected) return failed(n, shift, direction);
                ++cases;
            }
    BYTE* pages = (BYTE*)VirtualAlloc(NULL, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); DWORD old;
    if (!pages || !VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &old)) return 2;
    for (unsigned n : {0u, 1u, 2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 20u, 24u, 31u, 32u, 33u, 63u, 64u, 65u, 127u, 255u, 256u, 257u, 511u, 512u, 513u, 1023u, 2047u, 4063u})
        for (unsigned shift = 1; shift <= 32; ++shift) for (unsigned direction = 0; direction < 2; ++direction) {
            memcpy(pages, original.data(), 4096); memcpy(expected.data(), original.data(), 4096);
            unsigned src = 4096 - n - (direction ? shift : 0), dst = 4096 - n - (direction ? 0 : shift);
            fastMemmove(pages + dst, pages + src, n);
            o_crtMemmove(expected.data() + dst, expected.data() + src, n);
            if (memcmp(expected.data(), pages, 4096)) return failed(n, shift, direction);
            ++cases;
        }
    fastMemmove(pages + 4096, pages + 4096, 0);
    VirtualFree(pages, 0, MEM_RELEASE);
    printf("Overlapping memmove: %u native cases, complete-buffer/canary, protected-page and return-pointer checks passed\n", cases);
    tCrtCpy functions[] = {o_crtMemmove, previousMemmove, fastMemmove};
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    for (unsigned n : {16u, 24u, 31u, 64u, 127u, 512u, 4096u, 65536u})
        for (unsigned shift : {1u, 8u, 64u}) for (unsigned direction = 0; direction < 2; ++direction) {
            if (shift >= n) continue;
            unsigned src = 16 + (direction ? 0 : shift), dst = 16 + (direction ? shift : 0);
            unsigned iterations = n <= 512 ? 200000 : n <= 4096 ? 10000 : 1000;
            double ns[3];
            for (unsigned mode = 0; mode < 3; ++mode) {
                actual = original; tCrtCpy volatile function = functions[mode];
                LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
                for (unsigned i = 0; i < iterations; ++i) { function(actual.data() + dst, actual.data() + src, n); sink ^= actual[dst]; }
                QueryPerformanceCounter(&end);
                ns[mode] = (double)(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / iterations;
            }
            printf("memmove-overlap n=%u shift=%u %s: native=%.2f ns previous=%.2f ns new=%.2f ns previous/new=%.2fx\n", n, shift, direction ? "backward" : "forward", ns[0], ns[1], ns[2], ns[1] / ns[2]);
        }
    return 0;
}
