// Test-only snapshot of the previous 16-byte SIMD bounded comparison.
#pragma once
static __forceinline __m128i previousFoldAscii16(__m128i x) {
    __m128i upper = _mm_and_si128(_mm_cmpgt_epi8(x, _mm_set1_epi8('A' - 1)),
                                _mm_cmpgt_epi8(_mm_set1_epi8('Z' + 1), x));
    return _mm_or_si128(x, _mm_and_si128(upper, _mm_set1_epi8(32)));
}
static __forceinline unsigned previousCaseEvents(__m128i x, __m128i y) {
    unsigned equal = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(previousFoldAscii16(x), previousFoldAscii16(y)));
    unsigned zero = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(x, _mm_setzero_si128()));
    unsigned high = (unsigned)_mm_movemask_epi8(_mm_or_si128(x, y));
    return (~equal | zero | high) & 0xFFFF;
}
static __declspec(noinline) int __cdecl previousUnboundedCase(const char* a, const char* b) {
    const char* x = a; const char* y = b;
    for (;;) {
        if (CRT_PAGE_SAFE(x, y)) {
            unsigned events = previousCaseEvents(_mm_loadu_si128((const __m128i*)x), _mm_loadu_si128((const __m128i*)y));
            if (!events) { x += 16; y += 16; continue; }
            unsigned long k; _BitScanForward(&k, events);
            BYTE ca = (BYTE)x[k], cb = (BYTE)y[k];
            if ((ca | cb) & 0x80) return o_crtStricmp(a, b);
            int la = crtLowerAscii(ca), lb = crtLowerAscii(cb);
            return la == lb ? 0 : (la < lb ? -1 : 1);
        }
        BYTE ca = (BYTE)*x++, cb = (BYTE)*y++;
        if ((ca | cb) & 0x80) return o_crtStricmp(a, b);
        int la = crtLowerAscii(ca), lb = crtLowerAscii(cb);
        if (la != lb) return la < lb ? -1 : 1;
        if (!ca) return 0;
    }
}
static __declspec(noinline) int __cdecl previousBoundedCase(const char* a, const char* b, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (n - i >= 16 && CRT_PAGE_SAFE(a + i, b + i)) {
            unsigned events = previousCaseEvents(_mm_loadu_si128((const __m128i*)(a + i)), _mm_loadu_si128((const __m128i*)(b + i)));
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
