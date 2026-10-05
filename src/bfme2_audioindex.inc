#include "bfme2_features.h"
// Experimental BFME II 1.06 binding of the existing audio index.
// Shared list operations are hooked at entry; the index tracks only manager+0x98.
// Unlike the RotWK binding, this does not depend on enumerating audio call sites.
#include "bfme2_audio_profile.h"
static void installBfme2AudioIndex(BYTE* base) {
    if (!bfme2FeatureEnabled("BFME2_AUDIOINDEX")) {
        logf("bfme2-audio: experimental index OFF (BFME2_AUDIOINDEX=0 disables)."); return;
    }
    char env[8] = {};
    if (GetEnvironmentVariableA("AOTR_AUDIOLIMIT", env, sizeof(env)) && env[0] == '0') return;
    bool matches = false;
    __try { matches = bfme2AudioProfileMatches(base); }
    __except (EXCEPTION_EXECUTE_HANDLER) { matches = false; }
    if (!matches) { logf("bfme2-audio: retail code guard differs - OFF."); return; }

    BYTE* tramp[6] = {};
    for (int i = 0; i < 6; ++i) {
        tramp[i] = makeTrampoline((BYTE*)(ULONG_PTR)kBfme2AudioGuards[i].va, kBfme2AudioGuards[i].stolen);
        if (!tramp[i]) {
            for (int j = 0; j < i; ++j) VirtualFree(tramp[j], 0, MEM_RELEASE);
            logf("bfme2-audio: trampoline allocation failed - OFF."); return;
        }
    }
    void* hooks[6] = { (void*)&alThPushBack, (void*)&alThPushFront, (void*)&alThErase,
                      (void*)&alThPopBack, (void*)&alThClear, (void*)&hkAlPass };
    BYTE loop[15] = {0x57,0x53,0xE8,0,0,0,0,0x01,0x45,0xFC,0x01,0x45,0x08,0xEB,0x27};
    *(LONG*)(loop + 3) = (LONG)((DWORD)(ULONG_PTR)&alCount - (0x004576CD + 5));
    DWORD old[7] = {}; int protectedCount = 0;
    HANDLE threads[256]; int count = 0; suspendOthers(threads, &count, 256);
    // Acquire write access for every site before modifying any code or binding.
    for (int i = 0; i < 7; ++i) {
        const Bfme2AudioGuard& g = kBfme2AudioGuards[i];
        if (!VirtualProtect((void*)(ULONG_PTR)g.va, g.stolen, PAGE_EXECUTE_READWRITE, &old[i])) break;
        ++protectedCount;
    }
    bool stillMatches = bfme2AudioProfileMatches(base);
    if (protectedCount == 7 && stillMatches) {
        g_alPushBackAddr = (DWORD)(ULONG_PTR)tramp[0]; g_alPushFrontAddr = (DWORD)(ULONG_PTR)tramp[1];
        g_alEraseAddr = (DWORD)(ULONG_PTR)tramp[2]; g_alPopBackAddr = (DWORD)(ULONG_PTR)tramp[3];
        g_alClearAddr = (DWORD)(ULONG_PTR)tramp[4]; o_alPass = (tAlPass)tramp[5];
        for (int i = 0; i < 6; ++i) {
            const Bfme2AudioGuard& g = kBfme2AudioGuards[i]; BYTE* p = (BYTE*)(ULONG_PTR)g.va;
            p[0] = 0xE9; *(LONG*)(p + 1) = (LONG)((DWORD)(ULONG_PTR)hooks[i] - (g.va + 5));
            for (unsigned j = 5; j < g.stolen; ++j) p[j] = 0x90;
        }
        memcpy((void*)0x004576CB, loop, sizeof(loop)); g_alLive = 1;
    }
    for (int i = protectedCount - 1; i >= 0; --i) {
        const Bfme2AudioGuard& g = kBfme2AudioGuards[i]; DWORD ignored;
        VirtualProtect((void*)(ULONG_PTR)g.va, g.stolen, old[i], &ignored);
        FlushInstructionCache(GetCurrentProcess(), (void*)(ULONG_PTR)g.va, g.stolen);
    }
    resumeAll(threads, count);
    if (protectedCount != 7 || !stillMatches) {
        for (int i = 0; i < 6; ++i) VirtualFree(tramp[i], 0, MEM_RELEASE);
        logf("bfme2-audio: write access refused or guard changed - OFF (no code changed)."); return;
    }
    logf("bfme2-audio: experimental index installed; shared list mutations tracked; stock-answer proof and mismatch fallback retained. Runtime validation pending.");
}
