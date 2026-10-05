#include "bfme2_equiv_core.h"
#include "bfme2_equiv_profile.h"
static tIsEquiv o_bfme2Equiv = NULL;
static volatile LONG g_bfme2EquivOff = 0;
static __declspec(thread) unsigned g_bfme2EquivCalls = 0;
static BYTE __fastcall hkBfme2Equiv(void* self, void* edx, void* other) {
    if (g_bfme2EquivOff) return o_bfme2Equiv(self, edx, other);
    int result = bfme2EquivDecision((const BYTE*)self, (const BYTE*)other);
    if (result < 0) return o_bfme2Equiv(self, edx, other);
    // Proof state is per thread; result computation itself is stateless.
    unsigned call = ++g_bfme2EquivCalls;
    if (call <= 20000 || !(call & 63)) {
        BYTE stock = o_bfme2Equiv(self, edx, other);
        if (stock != result) {
            InterlockedExchange(&g_bfme2EquivOff, 1);
            logf("bfme2-equiv: fast answer differed - OFF; stock answer retained.");
            return stock;
        }
    }
    return (BYTE)result;
}
static void installBfme2EquivFast(BYTE* base) {
    char env[8] = {};
    if (!GetEnvironmentVariableA("BFME2_EQUIVFAST", env, sizeof(env)) || strcmp(env, "1")) {
        logf("bfme2-equiv: OFF (BFME2_EQUIVFAST=1 opts in)."); return;
    }
    bool matches = false;
    __try { matches = bfme2EquivProfileMatches(base); }
    __except (EXCEPTION_EXECUTE_HANDLER) { matches = false; }
    if (!matches) { logf("bfme2-equiv: code guard differs - OFF."); return; }
    BYTE* trampoline = makeTrampoline((BYTE*)0x0073BB04, 6);
    if (!trampoline) { logf("bfme2-equiv: allocation failed - OFF."); return; }
    HANDLE threads[256]; int count = 0; suspendOthers(threads, &count, 256);
    matches = bfme2EquivProfileMatches(base);
    BOOL installed = FALSE;
    if (matches) {
        o_bfme2Equiv = (tIsEquiv)trampoline;
        installed = patchJmp((BYTE*)0x0073BB04, (void*)&hkBfme2Equiv, 6);
    }
    resumeAll(threads, count);
    if (!installed) {
        o_bfme2Equiv = NULL; VirtualFree(trampoline, 0, MEM_RELEASE);
        logf("bfme2-equiv: guard changed or write access refused - OFF."); return;
    }
    logf("bfme2-equiv: opt-in empty-list fast path installed; populated lists use stock; shadow checks retained. In-game validation pending.");
}
