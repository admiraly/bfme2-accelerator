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
static int sortTests();
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
    return sortTests();
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
    return 0;
}
int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "child")) return child(argv[2]);
    if (argc == 3) return mappedChild(argv[1], argv[2]);
    return 2;
}
