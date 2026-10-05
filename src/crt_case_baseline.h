// Test-only snapshot of the previous 16-byte SIMD bounded comparison.
#pragma once
static __declspec(noinline) int __cdecl previousBoundedCase(const char* a, const char* b, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (n - i >= 16 && CRT_PAGE_SAFE(a + i, b + i)) {
            unsigned events = crtCaseEvents(_mm_loadu_si128((const __m128i*)(a + i)), _mm_loadu_si128((const __m128i*)(b + i)));
            if (!events) { i += 16; continue; }
            unsigned long k; _BitScanForward(&k, events);
            BYTE ca = (BYTE)a[i + k], cb = (BYTE)b[i + k];
            if ((ca | cb) & 0x80) return o_crtStrnicmp(a, b, n);
            int la = crtLowerAscii(ca), lb = crtLowerAscii(cb);
            return la == lb ? 0 : (la < lb ? -1 : 1);
        }
        BYTE ca = (BYTE)a[i], cb = (BYTE)b[i]; ++i;
        if ((ca | cb) & 0x80) return o_crtStrnicmp(a, b, n);
        int la = crtLowerAscii(ca), lb = crtLowerAscii(cb);
        if (la != lb) return la < lb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}
