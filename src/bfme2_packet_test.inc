// Executes the active inline blocks in the mapped native image. Test-only RET
// continuations isolate packet processing without starting sockets or the game.
static __declspec(naked) void __cdecl packetSendBlock(void*, int, DWORD) {
    __asm { push ebp }
    __asm { push ebx }
    __asm { push esi }
    __asm { push edi }
    __asm { mov esi, [esp+20] }
    __asm { mov edi, [esp+24] }
    __asm { mov ebx, [esp+28] }
    __asm { mov eax, 0x008D4F47 }
    __asm { call eax }
    __asm { pop edi }
    __asm { pop esi }
    __asm { pop ebx }
    __asm { pop ebp }
    __asm { ret }
}
static __declspec(naked) void __cdecl packetReceiveBlock(void*, int) {
    __asm { push ebp }
    __asm { push ebx }
    __asm { push esi }
    __asm { push edi }
    __asm { mov ebp, [esp+20] }
    __asm { add ebp, 0x438 }
    __asm { mov edi, [esp+24] }
    __asm { mov esi, 0x12345678 }
    __asm { mov eax, 0x008D4D87 }
    __asm { call eax }
    __asm { cmp esi, 0x12345678 }
    __asm { je preserved }
    __asm { int 3 }
    __asm { preserved: pop edi }
    __asm { pop esi }
    __asm { pop ebx }
    __asm { pop ebp }
    __asm { ret }
}
static __declspec(naked) void packetWrongOracle() {
    __asm { xor byte ptr [ecx], 0x55 }
    __asm { ret }
}
static int packetTests() {
    BYTE* image = (BYTE*)0x400000;
    if (!bfme2PacketProfileMatches(image)) return 1;
    HMODULE winsock = LoadLibraryA("wsock32.dll");
    FARPROC htonl = winsock ? GetProcAddress(winsock, "htonl") : NULL;
    if (!htonl) return 2;
    *(DWORD*)0xBBA998 = (DWORD)(ULONG_PTR)htonl;
    unsigned mutations = 0;
    const DWORD addresses[] = {0x8D4A8E, 0x8D4AC0, 0x8D4EC6, 0x8D4D08};
    const unsigned sizes[] = {50, 50, 180, 446};
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < sizes[i]; ++j) {
        BYTE& b = *(BYTE*)(ULONG_PTR)(addresses[i] + j); b ^= 1;
        bool rejected = !bfme2PacketProfileMatches(image); b ^= 1;
        if (!rejected) return 1;
        ++mutations;
    }
    BYTE original[1184], actual[1184], expected[1184];
    double inlineStock[8][2] = {};
    const unsigned inlineLengths[] = {4, 8, 12, 16, 32, 64, 128, 480};
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    unsigned cases = 0;
    // Every alignment and size through the native receive bound, plus signed
    // no-work inputs and larger standalone helper sizes.
    for (unsigned round = 0; round < 8; ++round) {
        for (unsigned i = 0; i < sizeof(original); ++i) original[i] = (BYTE)random32();
        for (int length = -7; length <= 1100; ++length) for (unsigned align = 0; align < 16; ++align) {
            for (unsigned decode = 0; decode < 2; ++decode) {
                memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
                bfme2PacketStock(expected + align + 16, length, g_bfme2PacketStock[decode]);
                if (decode) bfme2PacketTransform<true>(actual + align + 16, length);
                else bfme2PacketTransform<false>(actual + align + 16, length);
                if (memcmp(expected, actual, sizeof(actual))) return 1;
                ++cases;
            }
            memcpy(actual, original, sizeof(actual));
            bfme2PacketTransform<false>(actual + align + 16, length);
            bfme2PacketTransform<true>(actual + align + 16, length);
            if (memcmp(actual, original, sizeof(actual))) return 1;
        }
    }
    BYTE* pages = (BYTE*)VirtualAlloc(NULL, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pages) return 2;
    DWORD old;
    if (!VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &old)) return 2;
    for (int length = 0; length <= 1038; ++length) {
        BYTE* p = pages + 4096 - length;
        for (int i = 0; i < length; ++i) p[i] = original[i];
        memcpy(expected, original, length);
        bfme2PacketStock(expected, length, g_bfme2PacketStock[0]);
        bfme2PacketTransform<false>(p, length);
        if (length && memcmp(p, expected, length)) return 1;
        bfme2PacketStock(p, length, g_bfme2PacketStock[1]);
        if (length && memcmp(p, original, length)) return 1;
        ++cases;
    }
    bfme2PacketTransform<false>(pages + 4096, -2147483647 - 1);
    bfme2PacketTransform<true>(pages + 4096, -1);
    VirtualFree(pages, 0, MEM_RELEASE);
    // Exercise opt-out, code mismatch, write refusal before installing.
    SetEnvironmentVariableA("BFME2_NETFAST", NULL);
    installBfme2PacketFast(image);
    if (!bfme2PacketProfileMatches(image)) return 1;
    SetEnvironmentVariableA("BFME2_NETFAST", "1");
    *(BYTE*)0x8D4A8E ^= 1; installBfme2PacketFast(image); *(BYTE*)0x8D4A8E ^= 1;
    if (!bfme2PacketProfileMatches(image)) return 1;
    failPatch = true; installBfme2PacketFast(image); failPatch = false;
    if (!bfme2PacketProfileMatches(image)) return 1;
    for (unsigned direction = 0; direction < 2; ++direction) {
        DWORD sites[] = {0x8D4F47, 0x8D4D87};
        failPatchTarget = sites[direction]; installBfme2PacketFast(image); failPatchTarget = 0;
        if (*(BYTE*)(ULONG_PTR)sites[1-direction] != 0xE9 ||
            *(BYTE*)(ULONG_PTR)sites[direction] != (direction ? 0x8B : 0x8D)) return 1;
        memcpy((void*)0x8D4F47, kBfme2PacketSend + 0x8D4F47 - 0x8D4EC6, 6);
        memcpy((void*)0x8D4D87, kBfme2PacketReceive + 0x8D4D87 - 0x8D4D08, 6);
        FlushInstructionCache(GetCurrentProcess(), (void*)0x8D4D08, 0x272);
        if (!bfme2PacketProfileMatches(image)) return 1;
    }
    bfme2PacketStock(NULL, -2147483647 - 1, g_bfme2PacketStock[0]);
    bfme2PacketStock(NULL, 0, g_bfme2PacketStock[1]);
    unsigned proof = g_bfme2PacketCalls[0];
    memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
    bfme2PacketStock(expected, 1100, g_bfme2PacketStock[0]); bfme2PacketEncode(actual, 1100);
    if (memcmp(expected, actual, sizeof(actual)) || g_bfme2PacketCalls[0] != proof) return 1;
    // Native block equivalence before detouring; receive stack scratch slots
    // lie after the 1038-byte packet and are deliberately excluded here.
    *(BYTE*)0x8D4F75 = 0xC3; *(BYTE*)0x8D4DBF = 0xC3;
    for (unsigned n = 0; n <= 476; ++n) {
        memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
        DWORD crc = random32(); memcpy(expected, &crc, 4);
        bfme2PacketStock(expected, n + 4, g_bfme2PacketStock[0]);
        packetSendBlock(actual, n, crc);
        if (memcmp(expected, actual, sizeof(actual))) return 1;
    }
    for (int n = 0; n <= 1038; ++n) {
        memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
        bfme2PacketStock(expected, n, g_bfme2PacketStock[1]); packetReceiveBlock(actual, n);
        if (memcmp(expected, actual, 1038)) return 1;
    }
    for (unsigned size = 0; size < 8; ++size) for (unsigned decode = 0; decode < 2; ++decode) {
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        for (unsigned i = 0; i < 500000; ++i) {
            if (decode) packetReceiveBlock(actual, inlineLengths[size]);
            else packetSendBlock(actual, inlineLengths[size] - 4, 0x98765432);
            sink ^= actual[0];
        }
        QueryPerformanceCounter(&end);
        inlineStock[size][decode] = (double)(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 500000;
    }
    *(BYTE*)0x8D4F75 = kBfme2PacketSend[0x8D4F75 - 0x8D4EC6];
    *(BYTE*)0x8D4DBF = kBfme2PacketReceive[0x8D4DBF - 0x8D4D08];
    installBfme2PacketFast(image);
    if (*(BYTE*)0x8D4F47 != 0xE9 || *(BYTE*)0x8D4D87 != 0xE9) return 1;
    *(BYTE*)0x8D4F75 = 0xC3; *(BYTE*)0x8D4DBF = 0xC3;
    for (unsigned i = 0; i < 30000; ++i) {
        unsigned n = random32() % 477, align = random32() % 16;
        memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
        DWORD crc = random32(); memcpy(expected + align, &crc, 4);
        bfme2PacketStock(expected + align, n + 4, g_bfme2PacketStock[0]);
        packetSendBlock(actual + align, n, crc);
        if (memcmp(expected, actual, sizeof(actual))) return 1;
        n = random32() % 1039;
        memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
        bfme2PacketStock(expected + align, n, g_bfme2PacketStock[1]);
        packetReceiveBlock(actual + align, n);
        if (memcmp(expected, actual, align + 1038)) return 1;
        cases += 2;
    }
    for (unsigned align = 0; align < 16; ++align) for (unsigned n = 0; n < 16; ++n) {
        memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
        bfme2PacketStock(expected + align, n, g_bfme2PacketStock[1]); packetReceiveBlock(actual + align, n);
        if (memcmp(expected, actual, align + 1038)) return 1;
        if (n >= 4) {
            memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
            DWORD crc = random32(); memcpy(expected + align, &crc, 4);
            bfme2PacketStock(expected + align, n, g_bfme2PacketStock[0]); packetSendBlock(actual + align, n - 4, crc);
            if (memcmp(expected, actual, sizeof(actual))) return 1;
        }
    }
    if (g_bfme2PacketOff) return 1;
    // Check wrong oracle restores stock bytes and disables both directions.
    void* stock = g_bfme2PacketStock[0]; g_bfme2PacketStock[0] = (void*)&packetWrongOracle;
    g_bfme2PacketCalls[0] = 0;
    memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
    DWORD crc = 0x98765432; memcpy(expected, &crc, 4); expected[0] ^= 0x55;
    packetSendBlock(actual, 12, crc);
    if (!g_bfme2PacketOff || memcmp(expected, actual, sizeof(actual))) return 1;
    g_bfme2PacketStock[0] = stock;
    memcpy(actual, original, sizeof(actual)); memcpy(expected, original, sizeof(expected));
    bfme2PacketStock(expected, 100, g_bfme2PacketStock[1]); packetReceiveBlock(actual, 100);
    if (memcmp(expected, actual, 1038)) return 1;
    g_bfme2PacketOff = 0;
    printf("BFME II packet transforms: %u exact native cases, %u mutated guard bytes, protected pages, signed no-work inputs, real inline detours and mismatch restoration passed\n", cases, mutations);
    // Steady-state timings include the real detour bridge and one-in-64 proof.
    for (unsigned i = 0; i < 20000; ++i) {
        packetSendBlock(actual, 12, 0x98765432); packetReceiveBlock(actual, 16);
    }
    for (unsigned size = 0; size < 8; ++size) for (unsigned decode = 0; decode < 2; ++decode) {
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        for (unsigned i = 0; i < 500000; ++i) {
            if (decode) packetReceiveBlock(actual, inlineLengths[size]);
            else packetSendBlock(actual, inlineLengths[size] - 4, 0x98765432);
            sink ^= actual[0];
        }
        QueryPerformanceCounter(&end);
        double ns = (double)(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 500000;
        printf("packet-inline-%s len=%u: native=%.2f ns detoured-SIMD=%.2f ns ratio=%.2fx (steady-state synthetic CPU timing)\n", decode ? "decode" : "encode", inlineLengths[size], inlineStock[size][decode], ns, inlineStock[size][decode] / ns);
    }
    for (unsigned length : {16u, 64u, 128u, 480u, 1024u}) for (unsigned decode = 0; decode < 2; ++decode) {
        double ns[2] = {};
        for (unsigned mode = 0; mode < 2; ++mode) {
            LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
            for (unsigned i = 0; i < 500000; ++i) {
                if (!mode) bfme2PacketStock(actual, length, g_bfme2PacketStock[decode]);
                else if (decode) bfme2PacketDecode(actual, length);
                else bfme2PacketEncode(actual, length);
                sink ^= actual[0];
            }
            QueryPerformanceCounter(&end);
            ns[mode] = (double)(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 500000;
        }
        printf("packet-%s len=%u: native=%.2f ns checked-SIMD=%.2f ns ratio=%.2fx (synthetic CPU timing; not network latency)\n", decode ? "decode" : "encode", length, ns[0], ns[1], ns[0] / ns[1]);
    }
    return 0;
}
