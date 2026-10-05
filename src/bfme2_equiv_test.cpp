// Execute only the guarded original equivalence routine in a mapped test image.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "crt_locale_test.h"
typedef BYTE (__fastcall* tIsEquiv)(void*, void*, void*);
static void logf(const char*, ...) {}
static bool failAllocation = false, failPatch = false;
static DWORD failPatchTarget = 0;
static BYTE* makeTrampoline(BYTE* target, int stolen) {
    if (failAllocation) return NULL;
    BYTE* t = (BYTE*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;
    memcpy(t, target, stolen); t[stolen] = 0xE9;
    *(DWORD*)(t + stolen + 1) = (DWORD)(ULONG_PTR)(target + stolen) - (DWORD)(ULONG_PTR)(t + stolen + 5);
    FlushInstructionCache(GetCurrentProcess(), t, stolen + 5); return t;
}
static BOOL patchJmp(BYTE* target, void* destination, int stolen) {
    if (failPatch || (DWORD)(ULONG_PTR)target == failPatchTarget) return FALSE;
    DWORD protection;
    if (!VirtualProtect(target, stolen, PAGE_EXECUTE_READWRITE, &protection)) return FALSE;
    target[0] = 0xE9; *(DWORD*)(target + 1) = (DWORD)(ULONG_PTR)destination - (DWORD)(ULONG_PTR)(target + 5);
    for (int i = 5; i < stolen; ++i) target[i] = 0x90;
    DWORD ignored; VirtualProtect(target, stolen, protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, stolen); return TRUE;
}
static void suspendOthers(HANDLE*, int*, int) {}
static void resumeAll(HANDLE*, int) {}
#include "aotr_fastcrt.inc"
#include "bfme2_stringfast.inc"
#include "bfme2_packetfast.inc"
#include "bfme2_crcfast.inc"
#include "bfme2_crc32fast.inc"
#include "bfme2_equivfast.inc"
#include "aotr_rlsort_algo.h"
#include "bfme2_rlsort.inc"

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
static int installationTests();
static int sortTests();
static int stringTests();
#include "bfme2_packet_test.inc"
#include "bfme2_crc_test.inc"
#include "bfme2_crc32_test.inc"

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
    if (!compare || !crtPrepare(crt)) return 2;
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
    if (!fallback || fast <= 20000 || g_bfme2EquivCalls <= 20000) return 1;
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
    if (stringTests()) return 1;
    return sortTests();
}
typedef int (__cdecl* tBfme2StringCompare)(const char*, int, const char*, int, int);
static tBfme2StringCompare o_bfme2StringCompare = (tBfme2StringCompare)0x405841;
static int __cdecl boundedResult(const char* a, int la, const char* b, int lb, int) {
    int r = fastStrnicmp(a, b, la < lb ? la : lb);
    return r ? r : la - lb;
}
static int stringTests() {
    if (!bfme2StringProfileMatches((BYTE*)0x400000)) return 2;
    const DWORD guardAddresses[] = {0x405841, 0x406A00};
    const unsigned guardSizes[] = {sizeof(kBfme2StringCompare), sizeof(kBfme2StringNoCase)};
    for (unsigned guard = 0; guard < 2; ++guard) for (unsigned i = 0; i < guardSizes[guard]; ++i) {
        BYTE& value = *(BYTE*)(ULONG_PTR)(guardAddresses[guard] + i); value ^= 1;
        bool rejected = !bfme2StringProfileMatches((BYTE*)0x400000); value ^= 1;
        if (!rejected) return 1;
    }
    o_bfme2StringCompare = (tBfme2StringCompare)0x405841;
    BYTE* a = (BYTE*)VirtualAlloc(NULL, 12288, MEM_RESERVE, PAGE_NOACCESS);
    BYTE* b = (BYTE*)VirtualAlloc(NULL, 12288, MEM_RESERVE, PAGE_NOACCESS);
    if (!a || !b || !VirtualAlloc(a + 4096, 4096, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(b + 4096, 4096, MEM_COMMIT, PAGE_READWRITE)) return 2;
    unsigned long cases = 0;
    for (unsigned trial = 0; trial < 500000; ++trial) {
        unsigned la = random32() % 257, lb = random32() % 257;
        char* p = (char*)a + 8192 - la;
        char* q = (char*)b + 8192 - lb;
        unsigned mode = trial % 6;
        for (unsigned i = 0; i < la; ++i) p[i] = mode == 0 ? 'A' : mode == 1 ? 'x' : (char)(random32() % 256);
        for (unsigned i = 0; i < lb; ++i) q[i] = mode == 0 ? 'a' : mode == 1 ? 'x' : (char)(random32() % 256);
        // No readable terminator is required: these are length-bounded spans.
        for (int flag : {0,1,255}) {
            int stock = o_bfme2StringCompare(p, la, q, lb, flag);
            int fast = boundedResult(p, la, q, lb, flag);
            if (stock != fast || g_bfme2StringOff) { printf("String helper mismatch trial=%u\n", trial); return 1; }
            ++cases;
        }
    }
    // Every byte pair at each SIMD lane, with high bytes after the first event.
    for (unsigned lane = 0; lane < 16; ++lane) for (unsigned x = 0; x < 256; ++x) for (unsigned y = 0; y < 256; ++y) {
        char* p = (char*)a + 4096; char* q = (char*)b + 4096;
        memset(p, 'A', 64); memset(q, 'a', 64);
        p[lane] = (char)x; q[lane] = (char)y; p[lane + 1] = (char)0xC0; q[lane + 1] = (char)0xDF;
        for (unsigned n : {lane, lane + 1, 64u}) {
            int stock = o_bfme2StringCompare(p, n, q, n, 0);
            int fast = boundedResult(p, n, q, n, 0);
            if (stock != fast || g_bfme2StringOff) return 1;
            ++cases;
        }
    }
    printf("BFME II bounded string helper: %lu native cases, 86 guard mutations rejected, exact returns and protected-page bounds passed\n", cases);
    o_bfme2StringNoCase = (tBfme2StringNoCase)0x406A00;
    g_bfme2StringCalls = 0;
    unsigned long memberCases = 0;
    for (unsigned trial = 0; trial < 500000; ++trial) {
        unsigned la = random32() % 257, lb = random32() % 257;
        BYTE* pa = a + 8192 - la - 8; BYTE* pb = b + 8192 - lb - 8;
        *(DWORD*)pa = *(DWORD*)pb = 100;
        *(WORD*)(pa + 4) = *(WORD*)(pa + 6) = (WORD)la;
        *(WORD*)(pb + 4) = *(WORD*)(pb + 6) = (WORD)lb;
        for (unsigned i = 0; i < la; ++i) pa[8 + i] = trial % 4 == 0 ? 'A' : (BYTE)random32();
        for (unsigned i = 0; i < lb; ++i) pb[8 + i] = trial % 4 == 0 ? 'a' : (BYTE)random32();
        DWORD ha = trial % 17 == 0 ? 0 : (DWORD)(ULONG_PTR)pa;
        DWORD hb = trial % 23 == 0 ? 0 : (DWORD)(ULONG_PTR)pb;
        if (trial % 7 == 0) hb = ha; // Shared header, including non-ASCII data.
        int stock = o_bfme2StringNoCase(&ha, NULL, &hb);
        int fast = hkBfme2StringNoCase(&ha, NULL, &hb);
        if (stock != fast || g_bfme2StringOff) { printf("StringBase mismatch trial=%u\n", trial); return 1; }
        ++memberCases;
    }
    std::vector<BYTE> largeA(65543), largeB(65543);
    memset(largeA.data() + 8, 'A', 65535); memset(largeB.data() + 8, 'a', 65535);
    DWORD ha = (DWORD)(ULONG_PTR)largeA.data(), hb = (DWORD)(ULONG_PTR)largeB.data();
    for (unsigned la : {0u,32767u,32768u,65534u,65535u}) for (unsigned lb : {0u,32767u,32768u,65534u,65535u}) {
        *(WORD*)(largeA.data() + 4) = (WORD)la; *(WORD*)(largeB.data() + 4) = (WORD)lb;
        if (o_bfme2StringNoCase(&ha, NULL, &hb) != hkBfme2StringNoCase(&ha, NULL, &hb)) return 1;
        ++memberCases;
    }
    tCrtSetLocale setLocale = (tCrtSetLocale)GetProcAddress(GetModuleHandleA("msvcr71.dll"), "setlocale");
    if (!setLocale) return 2;
    unsigned localeCases = 0;
    BYTE* lpa = a + 4096; BYTE* lpb = b + 4096;
    *(WORD*)(lpa + 4) = *(WORD*)(lpb + 4) = 2;
    for (const char* locale : {"English_United States.1252", "Turkish", "German_Germany.1252", "C"}) {
        if (!crtTestLocale(setLocale, locale)) continue;
        for (unsigned x = 0; x < 256; ++x) for (unsigned y = 0; y < 256; ++y) {
            lpa[8] = (BYTE)x; lpb[8] = (BYTE)y; lpa[9] = 'I'; lpb[9] = 'i';
            for (unsigned shared = 0; shared < 2; ++shared) {
                DWORD ha = (DWORD)(ULONG_PTR)lpa, hb = (DWORD)(ULONG_PTR)(shared ? lpa : lpb);
                if (o_bfme2StringNoCase(&ha, NULL, &hb) != hkBfme2StringNoCase(&ha, NULL, &hb) || g_bfme2StringOff) return 1;
                ++localeCases;
            }
        }
    }
    if (!setLocale(LC_CTYPE, "C")) return 2;
    printf("BFME II StringBase locale changes: %u exact native cases, including shared headers\n", localeCases);
    printf("BFME II StringBase: %lu native-header cases, null headers, full 16-bit lengths and protected-page bounds passed\n", memberCases);
    // Compare against our existing accelerated import path as well as stock.
    for (unsigned length : {8u,16u,32u,64u,128u}) for (unsigned mode : {0u,1u,2u}) {
        BYTE* pa = a + 4096; BYTE* pb = b + 4096;
        *(WORD*)(pa + 4) = *(WORD*)(pb + 4) = (WORD)length;
        memset(pa + 8, 'A', length); memset(pb + 8, 'a', length);
        DWORD ha = (DWORD)(ULONG_PTR)pa, hb = mode == 1 ? ha : (DWORD)(ULONG_PTR)pb;
        if (mode == 2) pb[8] = 'b';
        double untouchedBest = 1e9, iatBest = 1e9, hookBest = 1e9;
        auto measure = [&](tBfme2StringNoCase fn) {
            LARGE_INTEGER f, begin, end; QueryPerformanceFrequency(&f);
            for (int i = 0; i < 10000; ++i) sink = fn(&ha, NULL, &hb);
            QueryPerformanceCounter(&begin);
            for (int i = 0; i < 1000000; ++i) sink = fn(&ha, NULL, &hb);
            QueryPerformanceCounter(&end);
            return double(end.QuadPart - begin.QuadPart) * 1e9 / f.QuadPart / 1000000;
        };
        for (unsigned round = 0; round < 5; ++round) {
            *(DWORD*)0xBBA690 = (DWORD)(ULONG_PTR)o_crtStrnicmp;
            double untouched = measure(o_bfme2StringNoCase);
            *(DWORD*)0xBBA690 = (DWORD)(ULONG_PTR)fastStrnicmp;
            double oldTime, hookTime;
            if (round & 1) { hookTime = measure(hkBfme2StringNoCase); oldTime = measure(o_bfme2StringNoCase); }
            else { oldTime = measure(o_bfme2StringNoCase); hookTime = measure(hkBfme2StringNoCase); }
            if (untouched < untouchedBest) untouchedBest = untouched;
            if (oldTime < iatBest) iatBest = oldTime;
            if (hookTime < hookBest) hookBest = hookTime;
        }
        printf("StringBase %s %3u bytes: untouched %.2f ns, current SIMD IAT %.2f ns, full hook %.2f ns, import/hook %.2fx\n",
               mode == 1 ? "shared header" : mode == 2 ? "early mismatch" : "equal ASCII",
               length, untouchedBest, iatBest, hookBest, iatBest / hookBest);
    }
    *(DWORD*)0xBBA690 = (DWORD)(ULONG_PTR)o_crtStrnicmp;
    VirtualFree(a, 0, MEM_RELEASE); VirtualFree(b, 0, MEM_RELEASE);
    return 0;
}
struct FakeMesh { DWORD vt; LONG refs; BYTE pad[0xC4 - 8]; DWORD model; BYTE pad2[0x310 - 0xC8]; DWORD instance; };
static std::vector<DWORD> sequence;
static bool __cdecl recordedLess(const RlEl* a, const RlEl* b) {
    sequence.push_back(a->w[1]); sequence.push_back(b->w[1]);
    DWORD ma = ((FakeMesh*)(ULONG_PTR)a->w[0])->model, mb = ((FakeMesh*)(ULONG_PTR)b->w[0])->model;
    return ma < mb;
}
static int sortTests() {
    if (!bfme2RlProfileMatches((BYTE*)0x400000)) return 2;
    unsigned mutations = 0;
    const DWORD addresses[] = { 0x57330A, 0x8C6F77 };
    const unsigned sizes[] = { sizeof(kBfme2RlFamily), sizeof(kBfme2RlMedian) };
    for (unsigned i = 0; i < 2; ++i) for (unsigned j = 0; j < sizes[i]; ++j) {
        BYTE& value = *(BYTE*)(ULONG_PTR)(addresses[i] + j); value ^= 1;
        bool rejected = !bfme2RlProfileMatches((BYTE*)0x400000); value ^= 1;
        if (!rejected) return 1;
        ++mutations;
    }
    kRlHeap = 0x574335; kRlLessModel = 0x57368A; kRlLessModelInst = 0x5736A7;
    o_bfme2RlSort = (tBfme2RlSort)0x574870;
    typedef void (__cdecl* Core)(RlEl*, RlEl*, int, int, tRlPred);
    Core core = (Core)0x5746C3;
    tBfme2RlSort finalPass = (tBfme2RlSort)0x57409D;
    FakeMesh meshes[128] = {};
    for (auto& mesh : meshes) mesh.refs = 1000000;
    const unsigned lengths[] = {0,1,2,3,15,16,17,31,32,63,64,127,256,511,1024};
    tRlPred predicates[] = {(tRlPred)(ULONG_PTR)kRlLessModel, (tRlPred)(ULONG_PTR)kRlLessModelInst, recordedLess};
    unsigned cases = 0;
    for (unsigned pattern = 0; pattern < 9; ++pattern) for (unsigned n : lengths) for (unsigned repeat = 0; repeat < 8; ++repeat) {
        for (unsigned i = 0; i < 128; ++i) {
            meshes[i].model = pattern == 0 ? 0 : pattern == 1 ? i % 2 : pattern == 2 ? i % 5 : pattern == 3 ? i : random32() % 128;
            meshes[i].instance = random32() % 8;
        }
        std::vector<RlEl> original(n + 1);
        for (unsigned i = 0; i < n; ++i) {
            unsigned m = pattern == 4 ? i % 128 : pattern == 5 ? 127 - i % 128 : pattern == 6 ? (i & 1 ? i % 128 : 0) : random32() % 128;
            original[i].w[0] = (DWORD)(ULONG_PTR)&meshes[m]; original[i].w[1] = i;
            for (unsigned j = 2; j < 8; ++j) original[i].w[j] = random32() & 1 ? (DWORD)(ULONG_PTR)&meshes[random32() % 128] : 0;
        }
        for (tRlPred pred : predicates) {
            auto stock = original, fast = original, hooked = original;
            sequence.clear(); o_bfme2RlSort(stock.data(), stock.data() + n, pred);
            auto stockSequence = sequence;
            sequence.clear(); rlFastSort(fast.data(), fast.data() + n, pred);
            if (memcmp(stock.data(), fast.data(), n * sizeof(RlEl)) || (pred == recordedLess && stockSequence != sequence)) {
                printf("Sort difference n=%u pattern=%u repeat=%u\n", n, pattern, repeat); return 1;
            }
            hkBfme2RlSort(hooked.data(), hooked.data() + n, pred);
            if (memcmp(stock.data(), hooked.data(), n * sizeof(RlEl)) || g_bfme2RlOff) return 1;
            // Force stock heap fallback and compare the entire resulting order.
            stock = original; fast = original; sequence.clear();
            core(stock.data(), stock.data() + n, 0, 0, pred);
            finalPass(stock.data(), stock.data() + n, pred);
            stockSequence = sequence; sequence.clear();
            RlLessCall less = {pred}; rlSortBudget(fast.data(), fast.data() + n, 0, less, pred);
            if (memcmp(stock.data(), fast.data(), n * sizeof(RlEl)) || (pred == recordedLess && stockSequence != sequence)) return 1;
            for (const auto& mesh : meshes) if (mesh.refs != 1000000) { puts("Reference count difference"); return 1; }
            ++cases;
        }
    }
    if (g_bfme2RlProof || !g_bfme2RlCalls || !g_rlHeapFalls) return 1;
    printf("BFME II render sort: %u cases, identical bytes/comparator sequences/refcounts, %u guard mutations rejected; forced heap fallback passed\n", cases, mutations);
    // Measure full runtime hooks including sampled stock-order comparisons.
    for (unsigned n : {64u, 256u, 1024u}) {
        std::vector<RlEl> input(n), work(n);
        for (unsigned i = 0; i < 128; ++i) { meshes[i].model = random32() % 128; meshes[i].instance = random32() % 8; }
        for (auto& e : input) { e.w[0] = (DWORD)(ULONG_PTR)&meshes[random32() % 128]; e.w[1] = random32(); for (unsigned j = 2; j < 8; ++j) e.w[j] = (DWORD)(ULONG_PTR)&meshes[random32() % 128]; }
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        double stockBest = 1e9, hookBest = 1e9;
        const int iterations = 2000;
        for (unsigned round = 0; round < 5; ++round) for (unsigned pass = 0; pass < 2; ++pass) {
            tBfme2RlSort fn = ((round + pass) & 1) ? hkBfme2RlSort : o_bfme2RlSort;
            LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
            for (int i = 0; i < iterations; ++i) { memcpy(work.data(), input.data(), n * sizeof(RlEl)); fn(work.data(), work.data() + n, predicates[0]); }
            QueryPerformanceCounter(&end);
            double ns = double(end.QuadPart - begin.QuadPart) * 1e9 / frequency.QuadPart / iterations;
            if (fn == o_bfme2RlSort && ns < stockBest) stockBest = ns;
            if (fn == hkBfme2RlSort && ns < hookBest) hookBest = ns;
        }
        printf("render sort %u records (includes input copy): stock %.2f ns, full hook %.2f ns, stock/hook %.2fx\n", n, stockBest, hookBest, stockBest / hookBest);
    }
    return installationTests();
}
static BYTE __fastcall wrongEquivalence(void*, void*, void*) { return 1; }
static void __cdecl wrongSort(RlEl* begin, RlEl*, tRlPred) { begin->w[1] ^= 0x55; }
static int __fastcall wrongString(void*, void*, void*) { return 123; }
static int installationTests() {
    // Before touching entry points, exercise opt-out and failure paths.
    SetEnvironmentVariableA("BFME2_STRINGFAST", "0");
    SetEnvironmentVariableA("BFME2_EQUIVFAST", "0"); SetEnvironmentVariableA("BFME2_RLSORT", "0");
    installBfme2EquivFast((BYTE*)0x400000); installBfme2RlSort((BYTE*)0x400000); installBfme2StringFast((BYTE*)0x400000);
    if (!bfme2EquivProfileMatches((BYTE*)0x400000) || !bfme2RlProfileMatches((BYTE*)0x400000) || !bfme2StringProfileMatches((BYTE*)0x400000)) return 1;
    SetEnvironmentVariableA("BFME2_STRINGFAST", NULL);
    SetEnvironmentVariableA("BFME2_EQUIVFAST", NULL); SetEnvironmentVariableA("BFME2_RLSORT", NULL);
    for (unsigned failure = 0; failure < 3; ++failure) {
        failAllocation = failure == 0; failPatch = failure == 1;
        if (failure == 2) { *(BYTE*)0x73BB04 ^= 1; *(BYTE*)0x574870 ^= 1; *(BYTE*)0x406A00 ^= 1; }
        installBfme2EquivFast((BYTE*)0x400000); installBfme2RlSort((BYTE*)0x400000); installBfme2StringFast((BYTE*)0x400000);
        if (failure == 2) { *(BYTE*)0x73BB04 ^= 1; *(BYTE*)0x574870 ^= 1; *(BYTE*)0x406A00 ^= 1; }
        if (!bfme2EquivProfileMatches((BYTE*)0x400000) || !bfme2RlProfileMatches((BYTE*)0x400000) || !bfme2StringProfileMatches((BYTE*)0x400000)) return 1;
    }
    failAllocation = failPatch = false;
    installBfme2EquivFast((BYTE*)0x400000); installBfme2RlSort((BYTE*)0x400000); installBfme2StringFast((BYTE*)0x400000);
    if (!o_bfme2Equiv || !o_bfme2RlSort || !o_bfme2StringNoCase || *(BYTE*)0x406A00 != 0xE9 || *(BYTE*)0x73BB04 != 0xE9 || *(BYTE*)0x574870 != 0xE9) return 1;
    tBfme2StringNoCase stringEntry = (tBfme2StringNoCase)0x406A00;
    FakeString sa = {}, sb = {};
    sa.refs = sb.refs = 100; sa.capacity = sb.capacity = 31;
    DWORD ha = (DWORD)(ULONG_PTR)&sa, hb = (DWORD)(ULONG_PTR)&sb;
    for (const char* left : {"Gondor", "", "A", "\xC0X"}) for (const char* right : {"gOnDoR", "", "Abc", "\xE0x"}) {
        strcpy_s(sa.data, left); strcpy_s(sb.data, right);
        sa.length = (WORD)strlen(left); sb.length = (WORD)strlen(right);
        if (stringEntry(&ha, NULL, &hb) != o_bfme2StringNoCase(&ha, NULL, &hb) || g_bfme2StringOff) return 1;
    }
    DWORD empty = 0;
    if (stringEntry(&empty, NULL, &empty) || stringEntry(&empty, NULL, &hb) != -(int)sb.length) return 1;
    tIsEquiv entry = (tIsEquiv)0x73BB04;
    FakeTemplate a = {}, b = {};
    if (entry(a.data, NULL, b.data) || !entry(a.data, NULL, a.data) || entry(NULL, NULL, NULL)) return 1;
    word(a, 4, (DWORD)(ULONG_PTR)b.data);
    if (!entry(a.data, NULL, b.data)) return 1;
    word(a, 4, 0);
    // Exercise populated equivalence fallback with both detours active. The
    // reference call disables just the string replacement for an independent oracle.
    DWORD namesA[] = {ha, hb}, namesB[] = {hb, ha};
    for (unsigned trial = 0; trial < 20000; ++trial) {
        sa.length = sb.length = 6;
        memcpy(sa.data, trial & 1 ? "Gondor" : "Mordor", 7);
        memcpy(sb.data, trial & 2 ? "gOnDoR" : "Rohan!", 7);
        word(a, 0x64, ha); word(b, 0x64, hb);
        for (unsigned offset : {0x330u,0x33Cu}) {
            word(a, offset, (DWORD)(ULONG_PTR)namesA); word(b, offset, (DWORD)(ULONG_PTR)namesB);
            word(a, offset + 4, (DWORD)(ULONG_PTR)(namesA + trial % 3));
            word(b, offset + 4, (DWORD)(ULONG_PTR)(namesB + (trial / 3) % 3));
        }
        g_bfme2StringOff = 1;
        BYTE expected = o_bfme2Equiv(a.data, NULL, b.data);
        g_bfme2StringOff = 0;
        if (entry(a.data, NULL, b.data) != expected || g_bfme2EquivOff || g_bfme2StringOff) return 1;
    }
    memset(a.data, 0, sizeof(a.data)); memset(b.data, 0, sizeof(b.data));
    // The original entry now detours into the hook; the saved function runs
    // through its copied prologue and relative jump back into original code.
    FakeMesh meshes[32] = {};
    std::vector<RlEl> data(32);
    for (unsigned i = 0; i < 32; ++i) { meshes[i].refs = 1000000; meshes[i].model = 31 - i; data[i].w[0] = (DWORD)(ULONG_PTR)&meshes[i]; data[i].w[1] = i; }
    auto stock = data;
    tRlPred pred = (tRlPred)(ULONG_PTR)kRlLessModel;
    o_bfme2RlSort(stock.data(), stock.data() + 32, pred);
    ((tBfme2RlSort)0x574870)(data.data(), data.data() + 32, pred);
    if (memcmp(stock.data(), data.data(), 32 * sizeof(RlEl))) return 1;
    for (unsigned n : {8192u, 8193u}) {
        std::vector<RlEl> large(n);
        for (unsigned i = 0; i < n; ++i) { large[i].w[0] = (DWORD)(ULONG_PTR)&meshes[random32() % 32]; large[i].w[1] = i; }
        auto expected = large;
        o_bfme2RlSort(expected.data(), expected.data() + n, pred);
        ((tBfme2RlSort)0x574870)(large.data(), large.data() + n, pred);
        if (memcmp(expected.data(), large.data(), n * sizeof(RlEl))) return 1;
    }
    // Unknown comparator goes stock exactly once, without shadow duplication.
    auto unknown = data;
    sequence.clear(); o_bfme2RlSort(data.data(), data.data() + 32, recordedLess);
    auto expectedSequence = sequence; sequence.clear();
    g_bfme2RlProof = 1;
    ((tBfme2RlSort)0x574870)(unknown.data(), unknown.data() + 32, recordedLess);
    if (sequence != expectedSequence || g_bfme2RlProof != 1 || memcmp(data.data(), unknown.data(), 32 * sizeof(RlEl))) return 1;
    for (const auto& mesh : meshes) if (mesh.refs != 1000000) return 1;
    // Deliberately wrong stock oracles exercise disable and answer restoration.
    tBfme2StringNoCase originalString = o_bfme2StringNoCase;
    o_bfme2StringNoCase = wrongString; g_bfme2StringCalls = 0;
    if (stringEntry(&ha, NULL, &hb) != 123 || !g_bfme2StringOff) return 1;
    o_bfme2StringNoCase = originalString; g_bfme2StringOff = 0;
    tIsEquiv originalEq = o_bfme2Equiv;
    o_bfme2Equiv = wrongEquivalence; g_bfme2EquivCalls = 0;
    if (entry(a.data, NULL, b.data) != 1 || !g_bfme2EquivOff) return 1;
    o_bfme2Equiv = originalEq; g_bfme2EquivOff = 0;
    tBfme2RlSort originalSort = o_bfme2RlSort;
    o_bfme2RlSort = wrongSort; g_bfme2RlProof = 1;
    RlEl element = {}; element.w[0] = (DWORD)(ULONG_PTR)&meshes[0];
    ((tBfme2RlSort)0x574870)(&element, &element + 1, pred);
    if (element.w[1] != 0x55 || !g_bfme2RlOff) return 1;
    o_bfme2RlSort = originalSort; g_bfme2RlOff = 0;
    puts("BFME II native detours: opt-out, guard/allocation/write failures, trampoline calls, 20,000 populated-list/string integration cases, size/comparator limits and all three mismatch fallbacks passed");
    return 0;
}
static int featureDefaultTests() {
    const char* names[] = {"BFME2_AUDIOINDEX", "BFME2_EQUIVFAST", "BFME2_RLSORT", "BFME2_STRINGFAST", "BFME2_NETFAST", "BFME2_CRCFAST", "BFME2_CRC32FAST"};
    for (const char* name : names) {
        SetEnvironmentVariableA(name, NULL);
        if (!bfme2FeatureEnabled(name)) return 1;
        SetEnvironmentVariableA(name, "0");
        if (bfme2FeatureEnabled(name)) return 1;
        SetEnvironmentVariableA(name, "1");
        if (!bfme2FeatureEnabled(name)) return 1;
        for (const char* bad : {"unexpected", "01", "true"}) {
            SetEnvironmentVariableA(name, bad);
            if (bfme2FeatureEnabled(name)) return 1;
        }
        SetEnvironmentVariableA(name, NULL);
    }
    puts("BFME II feature policy: all seven default ON, explicit opt-out and invalid-value refusal passed");
    return 0;
}
static int networkChild(const char* crtPath) {
    if (featureDefaultTests()) return 1;
    int result = child(crtPath);
    if (result) return result;
    result = packetTests();
    if (result) return result;
    result = crcTests();
    if (result) return result;
    return crc32Tests();
}
int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "child")) return networkChild(argv[2]);
    if (argc == 3) return mappedChild(argv[1], argv[2]);
    return 2;
}
