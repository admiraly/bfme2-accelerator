static int crcFailure(unsigned line) { printf("CRC validation failed at line %u\n", line); return 1; }
static DWORD __cdecl wrongCrc(const BYTE*, DWORD, DWORD) { return 0xFA17C0DE; }
static int crcTests() {
    BYTE* image = (BYTE*)0x400000;
    if (!bfme2CrcProfileMatches(image)) return crcFailure(__LINE__);
    tBfme2Crc native = (tBfme2Crc)0x7EC8F7;
    BYTE input[4160]; unsigned cases = 0;
    for (unsigned i = 0; i < 43; ++i) {
        BYTE& b = *(BYTE*)(0x7EC8F7 + i); b ^= 1;
        bool refused = !bfme2CrcProfileMatches(image); b ^= 1;
        if (!refused) return crcFailure(__LINE__);
    }
    for (unsigned batch = 0; batch < 4; ++batch) {
        for (unsigned i = 0; i < sizeof(input); ++i) input[i] = (BYTE)random32();
        for (unsigned n = 0; n <= 1024; ++n) for (unsigned align = 0; align < 16; ++align) {
            DWORD seed = random32();
            if (native(input + align, n, seed) != bfme2PacketCrc(input + align, n, seed)) return crcFailure(__LINE__);
            ++cases;
        }
    }
    for (DWORD n : {0u, 1u, 4u, 0x7fffffffu, 0xffffffffu}) {
        DWORD seed = random32();
        if (native(NULL, n, seed) != seed || bfme2PacketCrc(NULL, n, seed) != seed) return crcFailure(__LINE__);
        ++cases;
    }
    BYTE* pages = (BYTE*)VirtualAlloc(NULL, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD old;
    if (!pages || !VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &old)) return 2;
    for (unsigned n = 0; n <= 4096; ++n) {
        BYTE* p = pages + 4096 - n; memcpy(p, input, n);
        DWORD seed = random32();
        if (native(p, n, seed) != bfme2PacketCrc(p, n, seed)) return crcFailure(__LINE__);
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
    SetEnvironmentVariableA("BFME2_CRCFAST", "0"); installBfme2CrcFast(image);
    if (!bfme2CrcProfileMatches(image) || o_bfme2Crc) return crcFailure(__LINE__);
    SetEnvironmentVariableA("BFME2_CRCFAST", NULL);
    for (unsigned failure = 0; failure < 3; ++failure) {
        failAllocation = failure == 0; failPatch = failure == 1;
        if (failure == 2) *(BYTE*)0x7EC8F7 ^= 1;
        installBfme2CrcFast(image);
        if (failure == 2) *(BYTE*)0x7EC8F7 ^= 1;
        if (!bfme2CrcProfileMatches(image) || o_bfme2Crc) return crcFailure(__LINE__);
    }
    failAllocation = failPatch = false;
    installBfme2CrcFast(image);
    if (!o_bfme2Crc || *(BYTE*)0x7EC8F7 != 0xE9) return crcFailure(__LINE__);
    for (unsigned i = 0; i < 50000; ++i) {
        unsigned n = random32() % 4097, align = random32() % 16;
        DWORD seed = random32();
        if (native(input + align, n, seed) != o_bfme2Crc(input + align, n, seed)) return crcFailure(__LINE__);
        ++cases;
    }
    // The original logic RNG getter must hash the same six words without
    // consuming or modifying RNG state, even through the installed CRC detour.
    typedef DWORD (__cdecl* SeedCrc)(); SeedCrc getSeedCrc = (SeedCrc)0x633F70;
    for (unsigned i = 0; i < 10000; ++i) {
        DWORD seed[6]; for (unsigned j = 0; j < 6; ++j) seed[j] = random32();
        memcpy((void*)0xDBA3D0, seed, sizeof(seed));
        if (getSeedCrc() != o_bfme2Crc((BYTE*)seed, sizeof(seed), 0) || memcmp((void*)0xDBA3D0, seed, sizeof(seed))) return crcFailure(__LINE__);
        ++cases;
    }
    tBfme2Crc stock = o_bfme2Crc;
    o_bfme2Crc = wrongCrc; g_bfme2CrcCalls = 0;
    if (native(input, 24, 0) != 0xFA17C0DE || !g_bfme2CrcOff) return crcFailure(__LINE__);
    if (native(NULL, 0xffffffff, 123) != 0xFA17C0DE) return crcFailure(__LINE__);
    o_bfme2Crc = stock;
    if (native(input, 24, 0) != stock(input, 24, 0)) return crcFailure(__LINE__);
    g_bfme2CrcOff = 0;
    for (unsigned i = 0; i < 20000; ++i) sink ^= native(input, 24, i);
    if (g_bfme2CrcOff) return crcFailure(__LINE__);
    printf("BFME II rotate/add hash: %u native cases, 43 guard mutations, protected pages, real detour, RNG getter/state and mismatch fallbacks passed\n", cases);
    for (unsigned j = 0; j < 8; ++j) {
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        for (unsigned i = 0; i < 500000; ++i) sink ^= native(input, lengths[j], i);
        QueryPerformanceCounter(&end);
        double ns = (double)(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 500000;
        printf("hash-inline len=%u: native=%.2f ns full-hook=%.2f ns ratio=%.2fx (steady-state synthetic CPU timing)\n", lengths[j], nsStock[j], ns, nsStock[j] / ns);
    }
    return 0;
}
