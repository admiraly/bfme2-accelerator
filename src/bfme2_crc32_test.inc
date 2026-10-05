static int crc32Tests() {
    BYTE* image = (BYTE*)0x400000;
    if (!bfme2Crc32ProfileMatches(image)) return crcFailure(__LINE__);
    bfme2Crc32Tables(g_bfme2Crc32Table);
    tBfme2Crc32 native = (tBfme2Crc32)0xA19AC0;
    BYTE input[4160]; unsigned cases = 0;
    for (unsigned i = 0; i < 51; ++i) {
        BYTE& b = *(BYTE*)(0xA19AC0 + i); b ^= 1;
        bool refused = !bfme2Crc32ProfileMatches(image); b ^= 1;
        if (!refused) return crcFailure(__LINE__);
    }
    for (unsigned batch = 0; batch < 4; ++batch) {
        for (unsigned i = 0; i < sizeof(input); ++i) input[i] = (BYTE)random32();
        for (unsigned n = 0; n <= 1024; ++n) for (unsigned align = 0; align < 16; ++align) {
            DWORD seed = random32();
            if (native(input + align, n, seed) != bfme2Crc32(input + align, n, seed, g_bfme2Crc32Table)) return crcFailure(__LINE__);
            ++cases;
        }
    }
    if (native(NULL, 0, 123) != 123 || bfme2Crc32(NULL, 0, 123, g_bfme2Crc32Table) != 123) return crcFailure(__LINE__);
    BYTE* pages = (BYTE*)VirtualAlloc(NULL, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD old;
    if (!pages || !VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &old)) return 2;
    for (unsigned n = 0; n <= 4096; ++n) {
        BYTE* p = pages + 4096 - n; memcpy(p, input, n);
        DWORD seed = random32();
        if (native(p, n, seed) != bfme2Crc32(p, n, seed, g_bfme2Crc32Table)) return crcFailure(__LINE__);
        ++cases;
    }
    VirtualFree(pages, 0, MEM_RELEASE);
    const unsigned lengths[] = {0, 1, 4, 8, 24, 64, 480, 1024};
    double nsStock[8]; LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    for (unsigned j = 0; j < 8; ++j) {
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        for (unsigned i = 0; i < 500000; ++i) sink ^= native(input, lengths[j], i);
        QueryPerformanceCounter(&end);
        nsStock[j] = (double)(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 500000;
    }
    SetEnvironmentVariableA("BFME2_CRC32FAST", "0"); installBfme2Crc32Fast(image);
    if (!bfme2Crc32ProfileMatches(image) || o_bfme2Crc32) return crcFailure(__LINE__);
    SetEnvironmentVariableA("BFME2_CRC32FAST", NULL);
    for (unsigned failure = 0; failure < 3; ++failure) {
        failAllocation = failure == 0; failPatch = failure == 1;
        if (failure == 2) *(BYTE*)0xA19AC0 ^= 1;
        installBfme2Crc32Fast(image);
        if (failure == 2) *(BYTE*)0xA19AC0 ^= 1;
        if (!bfme2Crc32ProfileMatches(image) || o_bfme2Crc32) return crcFailure(__LINE__);
    }
    failAllocation = failPatch = false;
    for (unsigned i = 0; i < 1024; ++i) {
        BYTE& b = *(BYTE*)(0xDD5F38 + i); b ^= 1;
        installBfme2Crc32Fast(image); b ^= 1;
        if (!bfme2Crc32ProfileMatches(image) || o_bfme2Crc32) return crcFailure(__LINE__);
    }
    installBfme2Crc32Fast(image);
    if (!o_bfme2Crc32 || *(BYTE*)0xA19AC0 != 0xE9) return crcFailure(__LINE__);
    for (unsigned n = 0; n <= 8; ++n) for (unsigned align = 0; align < 16; ++align)
        for (DWORD seed : {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu}) {
            if (native(input + align, n, seed) != o_bfme2Crc32(input + align, n, seed)) return crcFailure(__LINE__);
            ++cases;
        }
    for (unsigned i = 0; i < 50000; ++i) {
        unsigned n = random32() % 4097, align = random32() % 16;
        DWORD seed = random32();
        if (native(input + align, n, seed) != o_bfme2Crc32(input + align, n, seed)) return crcFailure(__LINE__);
        ++cases;
    }
    for (unsigned n : {0u, 1u, 4u, 8u, 24u, 64u, 480u, 4096u}) {
        BYTE xmm[128]; WORD beforeCw, afterCw;
        __asm fnstcw beforeCw
        unsigned beforeMxcsr = _mm_getcsr();
        DWORD result = crcCaptureXmm(input, n, 0x12345678, (tBfme2Crc)native, xmm);
        unsigned afterMxcsr = _mm_getcsr(); __asm fnstcw afterCw
        if (result != o_bfme2Crc32(input, n, 0x12345678) || beforeMxcsr != afterMxcsr || beforeCw != afterCw) return crcFailure(__LINE__);
        for (BYTE b : xmm) if (b != 0xff) return crcFailure(__LINE__);
    }
    tBfme2Crc32 stock = o_bfme2Crc32;
    o_bfme2Crc32 = (tBfme2Crc32)wrongCrc; g_bfme2Crc32Calls = 0;
    if (native(input, 24, 0) != 0xFA17C0DE || !g_bfme2Crc32Off) return crcFailure(__LINE__);
    if (native(NULL, 0, 123) != 0xFA17C0DE) return crcFailure(__LINE__);
    o_bfme2Crc32 = stock;
    if (native(input, 24, 0) != stock(input, 24, 0)) return crcFailure(__LINE__);
    g_bfme2Crc32Off = 0;
    for (unsigned i = 0; i < 20000; ++i) sink ^= native(input, 24, i);
    if (g_bfme2Crc32Off) return crcFailure(__LINE__);
    printf("BFME II IEEE CRC32: %u native cases, 51 guard mutations, protected pages, real detour, XMM/control preservation and mismatch fallbacks passed\n", cases);
    for (unsigned j = 0; j < 8; ++j) {
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        for (unsigned i = 0; i < 500000; ++i) sink ^= native(input, lengths[j], i);
        QueryPerformanceCounter(&end);
        double ns = (double)(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 500000;
        printf("crc32-inline len=%u: native=%.2f ns full-hook=%.2f ns ratio=%.2fx (steady-state synthetic CPU timing)\n", lengths[j], nsStock[j], ns, nsStock[j] / ns);
    }
    return 0;
}
