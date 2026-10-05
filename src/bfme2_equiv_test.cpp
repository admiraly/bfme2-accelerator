// Execute only the guarded original equivalence routine in a mapped test image.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
typedef BYTE (__fastcall* tIsEquiv)(void*, void*, void*);
static void logf(const char*, ...) {}
static BYTE* makeTrampoline(BYTE*, int) { return NULL; }
static BOOL patchJmp(BYTE*, void*, int) { return FALSE; }
static void suspendOthers(HANDLE*, int*, int) {}
static void resumeAll(HANDLE*, int) {}
#include "bfme2_equivfast.inc"

static int mappedChild(const char* path, const char* crtPath) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 2;
    DWORD size = GetFileSize(file, NULL), got = 0;
    if (size < 4096 || size > 32 * 1024 * 1024) { CloseHandle(file); return 2; }
    std::vector<BYTE> data(size);
    BOOL read = ReadFile(file, data.data(), size, &got, NULL); CloseHandle(file);
    if (!read || got != size) return 2;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)data.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        (unsigned)dos->e_lfanew > size - sizeof(IMAGE_NT_HEADERS)) return 2;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.ImageBase != 0x400000 ||
        nt->OptionalHeader.SizeOfImage > 32 * 1024 * 1024 ||
        nt->OptionalHeader.SizeOfHeaders > size || nt->OptionalHeader.SizeOfHeaders > nt->OptionalHeader.SizeOfImage) return 2;
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    size_t sectionOffset = (BYTE*)section - data.data();
    if (sectionOffset > size || nt->FileHeader.NumberOfSections > (size - sectionOffset) / sizeof(*section)) return 2;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (section[i].PointerToRawData > size || section[i].SizeOfRawData > size - section[i].PointerToRawData ||
            section[i].VirtualAddress > nt->OptionalHeader.SizeOfImage ||
            section[i].SizeOfRawData > nt->OptionalHeader.SizeOfImage - section[i].VirtualAddress) return 2;
    }
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH);
    char command[2 * MAX_PATH + 32];
    sprintf_s(command, "\"%s\" child \"%s\"", exe, crtPath);
    STARTUPINFOA startup = { sizeof(startup) }; PROCESS_INFORMATION process = {};
    if (!CreateProcessA(exe, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, &process)) return 2;
    BYTE* image = (BYTE*)VirtualAllocEx(process.hProcess, (void*)0x400000, nt->OptionalHeader.SizeOfImage,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    bool ok = image == (BYTE*)0x400000; SIZE_T written;
    if (ok) ok = !!WriteProcessMemory(process.hProcess, image, data.data(), nt->OptionalHeader.SizeOfHeaders, &written);
    for (unsigned i = 0; ok && i < nt->FileHeader.NumberOfSections; ++i) {
        DWORD length = section[i].SizeOfRawData;
        if (section[i].Misc.VirtualSize && section[i].Misc.VirtualSize < length) length = section[i].Misc.VirtualSize;
        if (length) ok = !!WriteProcessMemory(process.hProcess, image + section[i].VirtualAddress,
                                              data.data() + section[i].PointerToRawData, length, &written);
    }
    if (ok) ok = !!FlushInstructionCache(process.hProcess, image, nt->OptionalHeader.SizeOfImage);
    if (ok) ok = ResumeThread(process.hThread) != (DWORD)-1;
    DWORD code = 2;
    if (ok) {
        if (WaitForSingleObject(process.hProcess, 120000) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
        else TerminateProcess(process.hProcess, 2);
    } else TerminateProcess(process.hProcess, 2);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return (int)code;
}

struct FakeTemplate { BYTE data[0x348]; };
struct FakeString { DWORD refs; WORD length, capacity; char data[32]; };
static void word(FakeTemplate& t, unsigned offset, DWORD value) { memcpy(t.data + offset, &value, 4); }
static unsigned rng = 0x12345678;
static unsigned random32() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static volatile unsigned sink = 0;
static double timing(tIsEquiv function, void* a, void* b) {
    LARGE_INTEGER frequency, begin, end; QueryPerformanceFrequency(&frequency);
    for (int i = 0; i < 10000; ++i) sink = function(a, NULL, b);
    QueryPerformanceCounter(&begin);
    for (int i = 0; i < 2000000; ++i) sink = function(a, NULL, b);
    QueryPerformanceCounter(&end);
    return double(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / 2000000;
}
static int child(const char* crtPath) {
    BYTE* image = (BYTE*)0x400000;
    if (!bfme2EquivProfileMatches(image)) return 2;
    // Test fail-closed guards using the same routine as the runtime installer.
    unsigned mutations = 0;
    const DWORD addresses[] = { 0x73BB04, 0x5E35DF };
    const unsigned sizes[] = { sizeof(kBfme2EquivBody), sizeof(kBfme2EquivFinal) };
    for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < sizes[i]; ++j) {
        BYTE& value = *(BYTE*)(ULONG_PTR)(addresses[i] + j); value ^= 1;
        bool rejected = !bfme2EquivProfileMatches(image); value ^= 1;
        if (!rejected) return 1;
        ++mutations;
    }
    HMODULE crt = LoadLibraryA(crtPath);
    FARPROC compare = crt ? GetProcAddress(crt, "_strnicmp") : NULL;
    if (!compare) return 2;
    // The original bounded string comparator calls this IAT slot. Resolve it
    // for the test image; no game startup, constructors or entry point run.
    *(DWORD*)0x00BBA690 = (DWORD)(ULONG_PTR)compare;
    o_bfme2Equiv = (tIsEquiv)0x0073BB04;
    FakeString strings[8] = {};
    const char* names[] = { "Gondor", "gOnDoR", "Rohan", "Mordor", "Elf", "", "\xC0X", "\xE0x" };
    for (unsigned i = 0; i < 8; ++i) {
        strcpy_s(strings[i].data, names[i]); strings[i].length = (WORD)strlen(names[i]);
        strings[i].capacity = 31; strings[i].refs = 100;
    }
    const unsigned count = 64;
    FakeTemplate templates[count]; DWORD lists[count][2][6];
    unsigned long cases = 0, fast = 0, fallback = 0;
    for (unsigned batch = 0; batch < 500; ++batch) {
        memset(templates, 0, sizeof(templates));
        for (unsigned i = 0; i < count; ++i) {
            word(templates[i], 0x64, (DWORD)(ULONG_PTR)&strings[random32() % 8]);
            if (i + 1 < count && random32() % 4 == 0)
                word(templates[i], 4, (DWORD)(ULONG_PTR)&templates[i + 1 + random32() % (count - i - 1)]);
            for (unsigned list = 0; list < 2; ++list) {
                unsigned length = batch % 2 ? random32() % 7 : 0;
                for (unsigned j = 0; j < 6; ++j) lists[i][list][j] = (DWORD)(ULONG_PTR)&strings[random32() % 8];
                unsigned offset = list ? 0x33C : 0x330;
                word(templates[i], offset, (DWORD)(ULONG_PTR)lists[i][list]);
                word(templates[i], offset + 4, (DWORD)(ULONG_PTR)(lists[i][list] + length));
                word(templates[i], offset + 8, (DWORD)(ULONG_PTR)(lists[i][list] + 6));
            }
        }
        // Include nulls and repeated pointers; mutate/reuse the same object
        // storage every batch to prove that there is no stale memo state.
        for (unsigned i = 0; i <= count; ++i) for (unsigned j = 0; j <= count; ++j) {
            BYTE* a = i == count ? NULL : templates[i].data;
            BYTE* b = j == count ? NULL : templates[j].data;
            int decision = bfme2EquivDecision(a, b);
            if (decision < 0) ++fallback; else ++fast;
            BYTE stock = o_bfme2Equiv(a, NULL, b), result = hkBfme2Equiv(a, NULL, b);
            if (stock != result || g_bfme2EquivOff) { printf("Mismatch batch=%u pair=%u,%u\n", batch, i, j); return 1; }
            ++cases;
        }
    }
    if (!fallback || fast <= 20000 || g_bfme2EquivProof || !g_bfme2EquivChecks) return 1;
    printf("BFME II equivalence: %lu native-oracle cases (%lu fast, %lu fallback), %u guard mutations rejected, no differences\n",
           cases, fast, fallback, mutations);
    memset(templates, 0, sizeof(templates));
    for (unsigned depth = 0; depth <= 8; depth += 4) {
        for (unsigned i = 0; i < 16; ++i) word(templates[i], 4, 0);
        for (unsigned i = 0; i < depth; ++i) {
            word(templates[i], 4, (DWORD)(ULONG_PTR)&templates[i + 1]);
            word(templates[16 + i], 4, (DWORD)(ULONG_PTR)&templates[17 + i]);
        }
        double stockBest = 1e9, fastBest = 1e9;
        for (unsigned round = 0; round < 5; ++round) {
            double stockTime, fastTime;
            if (round & 1) { fastTime = timing(hkBfme2Equiv, templates[0].data, templates[16].data); stockTime = timing(o_bfme2Equiv, templates[0].data, templates[16].data); }
            else { stockTime = timing(o_bfme2Equiv, templates[0].data, templates[16].data); fastTime = timing(hkBfme2Equiv, templates[0].data, templates[16].data); }
            if (stockTime < stockBest) stockBest = stockTime;
            if (fastTime < fastBest) fastBest = fastTime;
        }
        printf("empty lists, override depth %u: stock %.2f ns, full hook %.2f ns, stock/hook %.2fx\n",
               depth, stockBest, fastBest, stockBest / fastBest);
    }
    return 0;
}
int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "child")) return child(argv[2]);
    if (argc == 3) return mappedChild(argv[1], argv[2]);
    return 2;
}
