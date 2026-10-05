// Compare the runtime SIMD routines directly with the pinned VS2003 CRT.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <locale.h>
static void logf(const char*, ...) {}
#include "aotr_fastcrt.inc"
#include "crt_case_baseline.h"
static unsigned rng = 0x12345678;
static unsigned random32() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int failures = 0;
static unsigned long cases = 0;
static void check(const char* a, const char* b, size_t n) {
    int x = o_crtStricmp(a, b), y = fastStricmp(a, b);
    if (x != y && failures++ < 10) printf("stricmp mismatch: %d %d n=%u\n", x, y, (unsigned)n);
    x = o_crtStrnicmp(a, b, n); y = fastStrnicmp(a, b, n);
    if (x != y && failures++ < 10) printf("strnicmp mismatch: %d %d n=%u\n", x, y, (unsigned)n);
    cases += 2;
}
// The previous implementation is retained only as a benchmark reference.
static __declspec(noinline) int __cdecl scalarCase(const char* a, const char* b) {
    const BYTE* x = (const BYTE*)a; const BYTE* y = (const BYTE*)b;
    for (;;) {
        BYTE ca = *x++, cb = *y++;
        if ((ca | cb) & 0x80) return o_crtStricmp(a, b);
        int la = crtLowerAscii(ca), lb = crtLowerAscii(cb);
        if (la != lb) return la < lb ? -1 : 1;
        if (!ca) return 0;
    }
}
static volatile int sink = 0;
static double measure(tCrtSCmp fn, const char* a, const char* b) {
    const int iterations = 1000000;
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    for (int i = 0; i < 10000; ++i) sink = fn(a, b);
    QueryPerformanceCounter(&start);
    for (int i = 0; i < iterations; ++i) sink = fn(a, b);
    QueryPerformanceCounter(&end);
    return double(end.QuadPart - start.QuadPart) * 1e9 / frequency.QuadPart / iterations;
}
static double measureBounded(tCrtSNCmp fn, const char* a, const char* b, size_t n) {
    LARGE_INTEGER frequency, start, end; QueryPerformanceFrequency(&frequency);
    for (int i = 0; i < 10000; ++i) sink = fn(a, b, n);
    QueryPerformanceCounter(&start);
    for (int i = 0; i < 1000000; ++i) sink = fn(a, b, n);
    QueryPerformanceCounter(&end);
    return double(end.QuadPart - start.QuadPart) * 1e9 / frequency.QuadPart / 1000000;
}
int main(int argc, char** argv) {
    if (argc != 2 || !crtPrepare(LoadLibraryA(argv[1]))) return 2;
    SYSTEM_INFO si; GetSystemInfo(&si);
    if (si.dwPageSize != 4096) return 2;
    BYTE* a = (BYTE*)VirtualAlloc(NULL, 12288, MEM_RESERVE, PAGE_NOACCESS);
    BYTE* b = (BYTE*)VirtualAlloc(NULL, 12288, MEM_RESERVE, PAGE_NOACCESS);
    if (!a || !b || !VirtualAlloc(a + 4096, 4096, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(b + 4096, 4096, MEM_COMMIT, PAGE_READWRITE)) return 2;
    // Every pair of byte values at each SIMD lane; includes every high byte,
    // punctuation bordering A..Z, terminators and mismatches.
    for (unsigned lane = 0; lane < 16; ++lane) {
        for (unsigned x = 0; x < 256; ++x) for (unsigned y = 0; y < 256; ++y) {
            char* p = (char*)a + 4096; char* q = (char*)b + 4096;
            memset(p, 'A', 64); memset(q, 'a', 64); p[63] = q[63] = 0;
            p[lane] = (char)x; q[lane] = (char)y;
            // High bytes after an earlier decisive event must be ignored.
            p[lane + 1] = (char)0xC0; q[lane + 1] = (char)0xDF;
            check(p, q, lane); check(p, q, lane + 1); check(p, q, 64);
        }
    }
    // Independent alignments, page-tail strings, bounded counts ending before
    // a protected page, and different lengths. Faults fail the CI process.
    for (unsigned trial = 0; trial < 200000; ++trial) {
        unsigned la = random32() % 257, lb = random32() % 257;
        unsigned oa = random32() % 32, ob = random32() % 32;
        char* p = (char*)a + 8192 - la - 1 - oa;
        char* q = (char*)b + 8192 - lb - 1 - ob;
        for (unsigned i = 0; i < la; ++i) p[i] = (char)(1 + random32() % 255);
        for (unsigned i = 0; i < lb; ++i) q[i] = (char)(1 + random32() % 255);
        p[la] = q[lb] = 0;
        check(p, q, random32() % 300);
        unsigned n = 1 + random32() % 256;
        p = (char*)a + 8192 - n; q = (char*)b + 8192 - n;
        memset(p, 'A', n); memset(q, 'a', n); // no readable terminator
        int x = o_crtStrnicmp(p, q, n), y = fastStrnicmp(p, q, n);
        if (x != y) ++failures;
        ++cases;
    }
    typedef char* (__cdecl* SetLocale)(int, const char*);
    HMODULE crt = GetModuleHandleA("msvcr71.dll");
    SetLocale setLocale = (SetLocale)GetProcAddress(crt, "setlocale");
    if (!setLocale || !g_crtCTypeHandle) return 2;
    unsigned localeChecks = 0, nonCLocales = 0;
    // Exercise the CRT's actual locale state and restore C before timing.
    for (const char* locale : {"English_United States.1252", "Turkish_Turkey.1254", "German_Germany.1252", "C"}) {
        if (!setLocale(LC_CTYPE, locale)) { printf("Locale unavailable: %s\n", locale); continue; }
        if (strcmp(locale, "C") && crtAsciiLocale()) return 1;
        if (!strcmp(locale, "C") && !crtAsciiLocale()) return 1;
        if (strcmp(locale, "C")) ++nonCLocales;
        char p[3] = {}, q[3] = {};
        for (unsigned x = 0; x < 256; ++x) for (unsigned y = 0; y < 256; ++y) {
            p[0] = (char)x; q[0] = (char)y; p[1] = 'I'; q[1] = 'i';
            check(p, q, 2); ++localeChecks;
        }
    }
    if (!nonCLocales || !setLocale(LC_CTYPE, "C")) return 2;
    const volatile DWORD* handle = g_crtCTypeHandle; g_crtCTypeHandle = NULL;
    check("I", "i", 1); // Unknown CRT locale exports delegate to stock.
    g_crtCTypeHandle = handle;
    printf("CRT locale switching: %u byte-pair cases across %u non-C locales, missing-export fallback checked\n", localeChecks, nonCLocales);
    printf("SIMD case comparison: %lu exact-return cases, %d failures\n", cases, failures);
    if (failures) return 1;
    // Informational timings: hosted runners are not battle benchmarks. Best
    // of five alternating-order rounds reduces scheduler noise, with no gate.
    for (unsigned length = 8; length <= 256; length *= 2) {
        char* p = (char*)a + 4096; char* q = (char*)b + 4096;
        memset(p, 'A', length); memset(q, 'a', length); p[length] = q[length] = 0;
        double oldBest = 1e9, newBest = 1e9, crtBest = 1e9, previousSimdBest = 1e9;
        for (int round = 0; round < 5; ++round) {
            double oldTime, newTime;
            if (round & 1) { newTime = measure(fastStricmp, p, q); oldTime = measure(scalarCase, p, q); }
            else { oldTime = measure(scalarCase, p, q); newTime = measure(fastStricmp, p, q); }
            double crtTime = measure(o_crtStricmp, p, q);
            double previousSimdTime = measure(previousUnboundedCase, p, q);
            if (previousSimdTime < previousSimdBest) previousSimdBest = previousSimdTime;
            if (oldTime < oldBest) oldBest = oldTime;
            if (newTime < newBest) newBest = newTime;
            if (crtTime < crtBest) crtBest = crtTime;
        }
        printf("equal ASCII %3u bytes: CRT %.2f ns, previous %.2f ns, SIMD %.2f ns, previous/SIMD %.2fx\n",
               length, crtBest, oldBest, newBest, oldBest / newBest);
        printf("ASCII fold %3u bytes: previous SIMD %.2f ns, single-range SIMD %.2f ns, previous/new %.2fx\n",
               length, previousSimdBest, newBest, previousSimdBest / newBest);
    }
    for (unsigned length : {4u,8u,12u,16u,24u,32u,64u,128u}) for (unsigned mismatch : {0u,1u}) {
        char* p = (char*)a + 4096; char* q = (char*)b + 4096;
        memset(p, 'A', length); memset(q, 'a', length);
        if (mismatch) q[0] = 'b';
        double oldBest = 1e9, newBest = 1e9;
        for (unsigned round = 0; round < 5; ++round) {
            double oldTime, newTime;
            if (round & 1) { newTime = measureBounded(fastStrnicmp, p, q, length); oldTime = measureBounded(previousBoundedCase, p, q, length); }
            else { oldTime = measureBounded(previousBoundedCase, p, q, length); newTime = measureBounded(fastStrnicmp, p, q, length); }
            if (oldTime < oldBest) oldBest = oldTime;
            if (newTime < newBest) newBest = newTime;
        }
        printf("bounded %s %3u bytes: previous %.2f ns, narrow SIMD %.2f ns, previous/new %.2fx\n",
               mismatch ? "early mismatch" : "equal ASCII", length, oldBest, newBest, oldBest / newBest);
    }
    VirtualFree(a, 0, MEM_RELEASE); VirtualFree(b, 0, MEM_RELEASE);
    return 0;
}
