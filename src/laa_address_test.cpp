// Verify the actual WOW64 address-space effect in two x86 builds of this probe.
#include <windows.h>
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    if (argc != 2 || (strcmp(argv[1], "enabled") && strcmp(argv[1], "stock"))) return 2;
    BOOL wow64 = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &wow64) || !wow64) return 2;
    bool enabled = !strcmp(argv[1], "enabled");
    MEMORYSTATUSEX status = {}; status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return 2;
    if ((status.ullTotalVirtual > 0x80000000ull) != enabled) return 1;
    BYTE* requested = (BYTE*)(ULONG_PTR)0x90000000;
    BYTE* memory = (BYTE*)VirtualAlloc(requested, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!enabled) {
        if (memory) { VirtualFree(memory, 0, MEM_RELEASE); return 1; }
    } else {
        if (memory != requested) { if (memory) VirtualFree(memory, 0, MEM_RELEASE); return 1; }
        for (unsigned i = 0; i < 4096; ++i) memory[i] = (BYTE)(i ^ (i >> 8));
        for (unsigned i = 0; i < 4096; ++i) if (memory[i] != (BYTE)(i ^ (i >> 8))) return 1;
        if (!VirtualFree(memory, 0, MEM_RELEASE)) return 2;
    }
    printf("x86 %s: total user VA %llu bytes; allocation at 0x90000000 %s as expected\n",
           enabled ? "LAA" : "stock", status.ullTotalVirtual, enabled ? "read/written" : "refused");
    return 0;
}
