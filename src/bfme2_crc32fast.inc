#include "bfme2_features.h"
#include "bfme2_crc32_algo.h"
#include "bfme2_crc32_profile.h"
typedef DWORD (__cdecl* tBfme2Crc32)(const BYTE*, DWORD, DWORD);
static uint32_t g_bfme2Crc32Table[8][256];
static tBfme2Crc32 o_bfme2Crc32 = NULL;
static volatile LONG g_bfme2Crc32Off = 0;
static __declspec(thread) unsigned g_bfme2Crc32Calls = 0;
static DWORD __cdecl hkBfme2Crc32(const BYTE* p, DWORD n, DWORD seed) {
    if (g_bfme2Crc32Off) return o_bfme2Crc32(p, n, seed);
    if (!n) return seed;
    if (n <= 8) return bfme2Crc32(p, n, seed, g_bfme2Crc32Table);
    DWORD value = bfme2Crc32(p, n, seed, g_bfme2Crc32Table);
    unsigned call = ++g_bfme2Crc32Calls;
    if (call <= 20000 || !(call & 63)) {
        DWORD stock = o_bfme2Crc32(p, n, seed);
        if (value != stock) {
            InterlockedExchange(&g_bfme2Crc32Off, 1);
            logf("bfme2-crc32: result differed - OFF; stock hash retained.");
            return stock;
        }
    }
    return value;
}
static void installBfme2Crc32Fast(BYTE* base) {
    if (!bfme2FeatureEnabled("BFME2_CRC32FAST")) { logf("bfme2-crc32: OFF (BFME2_CRC32FAST=0 disables)."); return; }
    if ((DWORD)(ULONG_PTR)base != 0x400000) return;
    bfme2Crc32Tables(g_bfme2Crc32Table);
    bool matches = false;
    __try { matches = bfme2Crc32ProfileMatches(base) && !memcmp((void*)0x00DD5F38, g_bfme2Crc32Table[0], 1024); }
    __except (EXCEPTION_EXECUTE_HANDLER) { matches = false; }
    if (!matches) { logf("bfme2-crc32: code guard differs - OFF."); return; }
    BYTE* trampoline = makeTrampoline((BYTE*)0x00A19AC0, 5);
    if (!trampoline) { logf("bfme2-crc32: allocation failed - OFF."); return; }
    HANDLE threads[256]; int count = 0; suspendOthers(threads, &count, 256);
    matches = bfme2Crc32ProfileMatches(base) && !memcmp((void*)0x00DD5F38, g_bfme2Crc32Table[0], 1024);
    BOOL installed = FALSE;
    if (matches) {
        o_bfme2Crc32 = (tBfme2Crc32)trampoline;
        installed = patchJmp((BYTE*)0x00A19AC0, (void*)&hkBfme2Crc32, 5);
    }
    resumeAll(threads, count);
    if (!installed) {
        o_bfme2Crc32 = NULL; VirtualFree(trampoline, 0, MEM_RELEASE);
        logf("bfme2-crc32: guard changed or write refused - OFF."); return;
    }
    logf("bfme2-crc32: default exact IEEE CRC32 installed; native checksums and sampled stock checks retained.");
}
