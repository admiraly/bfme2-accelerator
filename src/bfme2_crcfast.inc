#include "bfme2_features.h"
#include "bfme2_crc_algo.h"
#include "bfme2_crc_profile.h"
typedef DWORD (__cdecl* tBfme2Crc)(const BYTE*, DWORD, DWORD);
static tBfme2Crc o_bfme2Crc = NULL;
static volatile LONG g_bfme2CrcOff = 0;
static __declspec(thread) unsigned g_bfme2CrcCalls = 0;
static DWORD __cdecl hkBfme2CrcChecked(const BYTE* p, DWORD n, DWORD seed) {
    if (g_bfme2CrcOff) return o_bfme2Crc(p, n, seed);
    if (!p || !n) return seed;
    DWORD value = bfme2PacketCrc(p, n, seed);
    unsigned call = ++g_bfme2CrcCalls;
    if (call <= 20000 || !(call & 63)) {
        DWORD stock = o_bfme2Crc(p, n, seed);
        if (value != stock) {
            InterlockedExchange(&g_bfme2CrcOff, 1);
            logf("bfme2-crc: result differed - OFF; stock hash retained.");
            return stock;
        }
    }
    return value;
}
// Tiny hashes avoid C setup and TLS proof overhead. Only integer volatile
// registers are changed; ESI is saved and restored, and XMM/x87 are untouched.
static __declspec(naked) DWORD __cdecl hkBfme2Crc(const BYTE*, DWORD, DWORD) {
    __asm { cmp g_bfme2CrcOff, 0 }
    __asm { jne checked }
    __asm { mov eax, [esp+12] }
    __asm { mov edx, [esp+4] }
    __asm { test edx, edx }
    __asm { je done }
    __asm { mov ecx, [esp+8] }
    __asm { test ecx, ecx }
    __asm { je done }
    __asm { cmp ecx, 8 }
    __asm { ja checked }
    __asm { push esi }
    __asm { movzx esi, byte ptr [edx+0] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { dec ecx }
    __asm { je shortDone }
    __asm { movzx esi, byte ptr [edx+1] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { dec ecx }
    __asm { je shortDone }
    __asm { movzx esi, byte ptr [edx+2] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { dec ecx }
    __asm { je shortDone }
    __asm { movzx esi, byte ptr [edx+3] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { dec ecx }
    __asm { je shortDone }
    __asm { movzx esi, byte ptr [edx+4] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { dec ecx }
    __asm { je shortDone }
    __asm { movzx esi, byte ptr [edx+5] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { dec ecx }
    __asm { je shortDone }
    __asm { movzx esi, byte ptr [edx+6] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { dec ecx }
    __asm { je shortDone }
    __asm { movzx esi, byte ptr [edx+7] }
    __asm { rol eax, 1 }
    __asm { add eax, esi }
    __asm { shortDone: pop esi }
    __asm { done: ret }
    __asm { checked: jmp hkBfme2CrcChecked }
}
static void installBfme2CrcFast(BYTE* base) {
    if (!bfme2FeatureEnabled("BFME2_CRCFAST")) { logf("bfme2-crc: OFF (BFME2_CRCFAST=0 disables)."); return; }
    if ((DWORD)(ULONG_PTR)base != 0x400000) return;
    bool matches = false;
    __try { matches = bfme2CrcProfileMatches(base); }
    __except (EXCEPTION_EXECUTE_HANDLER) { matches = false; }
    if (!matches) { logf("bfme2-crc: code guard differs - OFF."); return; }
    BYTE* trampoline = makeTrampoline((BYTE*)0x007EC8F7, 6);
    if (!trampoline) { logf("bfme2-crc: allocation failed - OFF."); return; }
    HANDLE threads[256]; int count = 0; suspendOthers(threads, &count, 256);
    matches = bfme2CrcProfileMatches(base);
    BOOL installed = FALSE;
    if (matches) {
        o_bfme2Crc = (tBfme2Crc)trampoline;
        installed = patchJmp((BYTE*)0x007EC8F7, (void*)&hkBfme2Crc, 6);
    }
    resumeAll(threads, count);
    if (!installed) {
        o_bfme2Crc = NULL; VirtualFree(trampoline, 0, MEM_RELEASE);
        logf("bfme2-crc: guard changed or write refused - OFF."); return;
    }
    logf("bfme2-crc: default exact rotate/add hash installed; RNG/packet hashes and sampled stock checks retained.");
}
