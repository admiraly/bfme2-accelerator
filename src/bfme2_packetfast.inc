#include "bfme2_packet_algo.h"
#include "bfme2_packet_profile.h"
static volatile LONG g_bfme2PacketOff = 0;
static __declspec(thread) unsigned g_bfme2PacketCalls[2] = {};
// Native private ABI: buffer in ECX, signed byte length in EAX, caller cleanup.
static __declspec(naked) void __cdecl bfme2PacketStock(void*, int, void*) {
    __asm { mov ecx, [esp+4] }
    __asm { mov eax, [esp+8] }
    __asm { mov edx, [esp+12] }
    __asm { jmp edx }
}
static void* g_bfme2PacketStock[2] = { (void*)0x008D4A8E, (void*)0x008D4AC0 };
template<bool Decode> static __declspec(noinline) void bfme2PacketShadow(void* buffer, int length) {
    BYTE expected[1038];
    memcpy(expected, buffer, length);
    bfme2PacketStock(expected, length, g_bfme2PacketStock[Decode]);
    bfme2PacketTransform<Decode>(buffer, length);
    if (memcmp(expected, buffer, length)) {
        memcpy(buffer, expected, length);
        InterlockedExchange(&g_bfme2PacketOff, 1);
        logf("bfme2-network: bytes differed - OFF; stock packet retained.");
    }
}
template<bool Decode> static void __cdecl bfme2PacketChecked(void* buffer, int length) {
    if (g_bfme2PacketOff || length < 4 || length > 1038) {
        bfme2PacketStock(buffer, length, g_bfme2PacketStock[Decode]); return;
    }
    unsigned call = ++g_bfme2PacketCalls[Decode];
    if (call <= 20000 || !(call & 63)) bfme2PacketShadow<Decode>(buffer, length);
    else bfme2PacketTransform<Decode>(buffer, length);
}
static void __fastcall bfme2PacketEncode(void* p, int n) { bfme2PacketChecked<false>(p, n); }
static void __fastcall bfme2PacketDecode(void* p, int n) { bfme2PacketChecked<true>(p, n); }
// Send block inputs: ESI = packet slot, EDI = payload length, EBX = CRC.
// Continuation returns AL=1 and restores EBX/EDI/ESI from its own stack.
static __declspec(naked) void hkBfme2PacketSend() {
    __asm { cmp g_bfme2PacketOff, 0 }
    __asm { jne sendChecked }
    __asm { cmp edi, 32 }
    __asm { jae sendChecked }
    // Up to eight words: no C call, vector setup or partial CRC-store/SIMD-load.
    __asm { mov eax, ebx }
    __asm { xor eax, 0x38D9B7D4 }
    __asm { bswap eax }
    __asm { mov [esi+0], eax }
    __asm { cmp edi, 4 }
    __asm { jb sendDone }
    __asm { mov eax, [esi+4] }
    __asm { xor eax, 0xB99FF2C6 }
    __asm { bswap eax }
    __asm { mov [esi+4], eax }
    __asm { cmp edi, 8 }
    __asm { jb sendDone }
    __asm { mov eax, [esi+8] }
    __asm { xor eax, 0x3A662DB8 }
    __asm { bswap eax }
    __asm { mov [esi+8], eax }
    __asm { cmp edi, 12 }
    __asm { jb sendDone }
    __asm { mov eax, [esi+12] }
    __asm { xor eax, 0xBB2C68AA }
    __asm { bswap eax }
    __asm { mov [esi+12], eax }
    __asm { cmp edi, 16 }
    __asm { jb sendDone }
    __asm { mov eax, [esi+16] }
    __asm { xor eax, 0x3BF2A39C }
    __asm { bswap eax }
    __asm { mov [esi+16], eax }
    __asm { cmp edi, 20 }
    __asm { jb sendDone }
    __asm { mov eax, [esi+20] }
    __asm { xor eax, 0xBCB8DE8E }
    __asm { bswap eax }
    __asm { mov [esi+20], eax }
    __asm { cmp edi, 24 }
    __asm { jb sendDone }
    __asm { mov eax, [esi+24] }
    __asm { xor eax, 0x3D7F1980 }
    __asm { bswap eax }
    __asm { mov [esi+24], eax }
    __asm { cmp edi, 28 }
    __asm { jb sendDone }
    __asm { mov eax, [esi+28] }
    __asm { xor eax, 0xBE455472 }
    __asm { bswap eax }
    __asm { mov [esi+28], eax }
    __asm { jmp sendDone }
    __asm { sendChecked: mov [esi], ebx }
    __asm { lea edx, [edi+4] }
    __asm { mov ecx, esi }
    __asm { call bfme2PacketEncode }
    __asm { sendDone: mov eax, 0x008D4F75 }
    __asm { jmp eax }
}
// Receive block inputs: EDI = datagram length, buffer = EBP-0x438.
// ESI/EDI/EBP remain live; EBX, flags, EAX/ECX/EDX and the two loop-local
// scratch slots are dead at the continuation (verified through the epilogue).
static __declspec(naked) void hkBfme2PacketReceive() {
    __asm { cmp g_bfme2PacketOff, 0 }
    __asm { jne receiveChecked }
    __asm { cmp edi, 16 }
    __asm { jae receiveChecked }
    __asm { cmp edi, 4 }
    __asm { jb receiveDone }
    __asm { mov eax, [ebp-0x438] }
    __asm { bswap eax }
    __asm { xor eax, 0x38D9B7D4 }
    __asm { mov [ebp-0x438], eax }
    __asm { cmp edi, 8 }
    __asm { jb receiveDone }
    __asm { mov eax, [ebp-0x434] }
    __asm { bswap eax }
    __asm { xor eax, 0xB99FF2C6 }
    __asm { mov [ebp-0x434], eax }
    __asm { cmp edi, 12 }
    __asm { jb receiveDone }
    __asm { mov eax, [ebp-0x430] }
    __asm { bswap eax }
    __asm { xor eax, 0x3A662DB8 }
    __asm { mov [ebp-0x430], eax }
    __asm { jmp receiveDone }
    __asm { receiveChecked: mov edx, edi }
    __asm { lea ecx, [ebp-0x438] }
    __asm { call bfme2PacketDecode }
    __asm { receiveDone: mov eax, 0x008D4DBF }
    __asm { jmp eax }
}
static void installBfme2PacketFast(BYTE* base) {
    char env[8] = {};
    if (!GetEnvironmentVariableA("BFME2_NETFAST", env, sizeof(env)) || strcmp(env, "1")) {
        logf("bfme2-network: OFF (BFME2_NETFAST=1 opts in)."); return;
    }
    if ((DWORD)(ULONG_PTR)base != 0x400000) return;
    bool matches = false;
    __try { matches = bfme2PacketProfileMatches(base); }
    __except (EXCEPTION_EXECUTE_HANDLER) { matches = false; }
    if (!matches) { logf("bfme2-network: code guard differs - OFF."); return; }
    HANDLE threads[256]; int count = 0; suspendOthers(threads, &count, 256);
    matches = bfme2PacketProfileMatches(base);
    BOOL send = FALSE, receive = FALSE;
    if (matches) {
        // These are independent, byte-identical transforms: either site can
        // remain stock if its write is refused. No trampoline allocation.
        send = patchJmp((BYTE*)0x008D4F47, (void*)&hkBfme2PacketSend, 6);
        receive = patchJmp((BYTE*)0x008D4D87, (void*)&hkBfme2PacketReceive, 6);
    }
    resumeAll(threads, count);
    logf("bfme2-network: opt-in packet transforms send=%d receive=%d; sampled stock checks retained. Multiplayer validation pending.", send, receive);
}
