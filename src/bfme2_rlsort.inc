// BFME II binding: sort only. Push/erase/flush donor hooks stay disabled.
#include "bfme2_rlsort_profile.h"
typedef void (__cdecl* tBfme2RlSort)(RlEl*, RlEl*, tRlPred);
static tBfme2RlSort o_bfme2RlSort = NULL;
static volatile LONG g_bfme2RlOff = 0;
static __declspec(thread) unsigned g_bfme2RlProof = 32;
static __declspec(thread) unsigned g_bfme2RlCalls = 0;
static void __cdecl hkBfme2RlSort(RlEl* begin, RlEl* end, tRlPred pred) {
    size_t n = end - begin;
    if (!n) return;
    if (g_bfme2RlOff || n > 8192 ||
        ((DWORD)(ULONG_PTR)pred != kRlLessModel && (DWORD)(ULONG_PTR)pred != kRlLessModelInst)) { o_bfme2RlSort(begin, end, pred); return; }
    bool check = g_bfme2RlProof || !(++g_bfme2RlCalls & 255);
    RlEl* stock = NULL;
    if (check) {
        stock = (RlEl*)HeapAlloc(GetProcessHeap(), 0, n * sizeof(RlEl));
        if (!stock) { o_bfme2RlSort(begin, end, pred); return; }
        memcpy(stock, begin, n * sizeof(RlEl));
    }
    rlFastSort(begin, end, pred);
    if (check) {
        // Sorting only permutes owning references. The temporary sort's
        // increments and decrements balance while the live vector owns them.
        o_bfme2RlSort(stock, stock + n, pred);
        if (g_bfme2RlProof) --g_bfme2RlProof;
        if (memcmp(begin, stock, n * sizeof(RlEl))) {
            memcpy(begin, stock, n * sizeof(RlEl));
            InterlockedExchange(&g_bfme2RlOff, 1);
            logf("bfme2-rlsort: ordering differed - OFF; stock ordering retained.");
        }
        HeapFree(GetProcessHeap(), 0, stock);
    }
}
static void installBfme2RlSort(BYTE* base) {
    char env[8] = {};
    if (!GetEnvironmentVariableA("BFME2_RLSORT", env, sizeof(env)) || strcmp(env, "1")) {
        logf("bfme2-rlsort: OFF (BFME2_RLSORT=1 opts in)."); return;
    }
    bool matches = false;
    __try { matches = bfme2RlProfileMatches(base); }
    __except (EXCEPTION_EXECUTE_HANDLER) { matches = false; }
    if (!matches) { logf("bfme2-rlsort: code guards differ - OFF."); return; }
    BYTE* trampoline = makeTrampoline((BYTE*)0x00574870, 5);
    if (!trampoline) { logf("bfme2-rlsort: allocation failed - OFF."); return; }
    HANDLE threads[256]; int count = 0; suspendOthers(threads, &count, 256);
    matches = bfme2RlProfileMatches(base);
    BOOL installed = FALSE;
    if (matches) {
        kRlHeap = 0x00574335; kRlLessModel = 0x0057368A; kRlLessModelInst = 0x005736A7;
        o_bfme2RlSort = (tBfme2RlSort)trampoline;
        installed = patchJmp((BYTE*)0x00574870, (void*)&hkBfme2RlSort, 5);
    }
    resumeAll(threads, count);
    if (!installed) {
        kRlHeap = 0x00573B95; kRlLessModel = 0x00572EEA; kRlLessModelInst = 0x00572F07;
        o_bfme2RlSort = NULL; VirtualFree(trampoline, 0, MEM_RELEASE);
        logf("bfme2-rlsort: guard changed or write access refused - OFF."); return;
    }
    logf("bfme2-rlsort: opt-in 32-byte sort installed; stock heap fallback and ordering checks retained. In-game validation pending.");
}
