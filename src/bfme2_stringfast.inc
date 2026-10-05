#include "bfme2_features.h"
// Direct binding for native StringBase case-insensitive comparisons.
// Current headers are read on every call; locale-sensitive bytes retain CRT behavior.
#include "bfme2_string_profile.h"
static volatile LONG g_bfme2StringOff = 0;
static __declspec(thread) unsigned g_bfme2StringCalls = 0;
typedef int (__fastcall* tBfme2StringNoCase)(void*, void*, void*);
static tBfme2StringNoCase o_bfme2StringNoCase = NULL;
static int __fastcall hkBfme2StringNoCase(void* self, void* edx, void* other) {
    if (g_bfme2StringOff) return o_bfme2StringNoCase(self, edx, other);
    DWORD ha = *(DWORD*)self, hb = *(DWORD*)other;
    int result = 0;
    // Shared headers necessarily contain the same bytes and length. This also
    // covers two empty headers without entering the bounded comparison.
    if (ha != hb) {
        unsigned la = ha ? *(WORD*)(ULONG_PTR)(ha + 4) : 0;
        unsigned lb = hb ? *(WORD*)(ULONG_PTR)(hb + 4) : 0;
        const char* a = ha ? (const char*)(ULONG_PTR)(ha + 8) : "";
        const char* b = hb ? (const char*)(ULONG_PTR)(hb + 8) : "";
        result = fastStrnicmp(a, b, la < lb ? la : lb);
        if (!result) result = (int)la - (int)lb;
    }
    unsigned call = ++g_bfme2StringCalls;
    if (call <= 20000 || !(call & 63)) {
        int stock = o_bfme2StringNoCase(self, edx, other);
        if (stock != result) {
            InterlockedExchange(&g_bfme2StringOff, 1);
            logf("bfme2-string: result differed - OFF; stock return retained.");
            return stock;
        }
    }
    return result;
}
static void installBfme2StringFast(BYTE* base) {
    if (!bfme2FeatureEnabled("BFME2_STRINGFAST")) {
        logf("bfme2-string: OFF (BFME2_STRINGFAST=0 disables)."); return;
    }
    if (!o_crtStrnicmp || (DWORD)(ULONG_PTR)base != 0x400000) {
        logf("bfme2-string: CRT or image-base prerequisite missing - OFF."); return;
    }
    bool matches = false;
    __try { matches = bfme2StringProfileMatches(base); }
    __except (EXCEPTION_EXECUTE_HANDLER) { matches = false; }
    if (!matches) { logf("bfme2-string: code guard differs - OFF."); return; }
    BYTE* trampoline = makeTrampoline((BYTE*)0x00406A00, 6);
    if (!trampoline) { logf("bfme2-string: allocation failed - OFF."); return; }
    HANDLE threads[256]; int count = 0; suspendOthers(threads, &count, 256);
    matches = bfme2StringProfileMatches(base);
    BOOL installed = FALSE;
    if (matches) {
        o_bfme2StringNoCase = (tBfme2StringNoCase)trampoline;
        installed = patchJmp((BYTE*)0x00406A00, (void*)&hkBfme2StringNoCase, 6);
    }
    resumeAll(threads, count);
    if (!installed) {
        o_bfme2StringNoCase = NULL; VirtualFree(trampoline, 0, MEM_RELEASE);
        logf("bfme2-string: guard changed or write access refused - OFF."); return;
    }
    logf("bfme2-string: default StringBase comparison installed; exact return/locale handling and sampled stock checks retained. In-game validation pending.");
}
