// Offline model test of the v26 logic slicer (aotr_logicspread.inc).
// A model of GameLogic::update(n) - same structure as the engine's function at the points the slicer steers - records
// every operation it performs. For random list sizes, budgets, clock speeds and engine call patterns, the operation
// sequence of each step with the slicer ON must be IDENTICAL to the sequence with the slicer OFF (the stock order),
// and every module must be updated exactly once per step.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
static void logf(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }
static BYTE* makeTrampoline(BYTE*, int) { return NULL; }
static BOOL patchJmp(BYTE*, void*, int) { return FALSE; }
static void suspendOthers(HANDLE*, int* n, int) { *n = 0; }
static void resumeAll(HANDLE*, int) {}
// The model never installs live hooks, but the included installer must compile.
static const char* aotrPath(char* out, const char* name) { lstrcpynA(out, name, MAX_PATH); return out; }
static double g_tscPerQpc = 1.0; static LARGE_INTEGER g_pqpf;
#define SUB_N 12
static volatile LONG64 g_subT[SUB_N]; static DWORD g_subGlob[SUB_N];
static unsigned long long g_fakeTsc = 1000;
#define __rdtsc() (g_fakeTsc)

static void __fastcall modelUpdate(DWORD self, void* edx, DWORD n);
#define LS_UPDATE_ADDR (&modelUpdate)
#include "aotr_logicspread.inc"

// ---- the model
static unsigned g_rng = 777;
static unsigned rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
struct Op { int kind, a, b; bool operator==(const Op& o) const { return kind == o.kind && a == o.a && b == o.b; } };
enum { OP_VECCLEAR = 1, OP_PATHQ, OP_N1, OP_N2, OP_MODULE, OP_CLEANUP, OP_AI, OP_DESTROY, OP_SUBS, OP_TAIL, OP_TAIL1 };
static std::vector<Op> g_ops;
static void op(int k, int a = 0, int b = 0) { Op o = { k, a, b }; g_ops.push_back(o); }
static DWORD g_listMem[4][2];                     // begin / end pointers, as the engine's vectors (count = (end - begin) / 4)
static int g_count[4];
static BYTE g_fakeLogic[0x200];                   // +0x40 frame counter, +0x110 mode, +0x125 paused
static int g_costModule = 50, g_costN1 = 3000, g_costN2 = 6000, g_costSubs = 15000, g_costAI = 2000;
static int g_vecPending = 0;                      // entries in the "+0x164 vector": filled by modules, consumed by the destroy step; a clear that finds entries loses them
static int g_lost = 0;

static void body(DWORD self, DWORD n) {
    // pre-work (P1: vector clear; conditional exactly as the stub: skipped while a logical call is unfinished)
    if (!(g_lsResume || g_lsPaused)) { if (g_vecPending) { g_lost += g_vecPending; g_vecPending = 0; } op(OP_VECCLEAR, n); }
    if (g_fakeLogic[0x125] && !(*(DWORD*)(g_fakeLogic + 0x110) == 1 || *(DWORD*)(g_fakeLogic + 0x110) == 5)) return;   // single-player pause: early out
    if (n == 1) { ++*(DWORD*)(g_fakeLogic + 0x40); }
    if (!g_lsResume) op(OP_PATHQ, n);                                              // P2a / P2b
    if (n == 1) { op(OP_N1); g_fakeTsc += g_costN1; }
    if (n == 2) { op(OP_N2); g_fakeTsc += g_costN2; }
    LONG last = 0; int first = lsBegin(self, n, &last);
    if (first != -1) {
        for (int k = first; k < last; ++k) {
            const DWORD* hdr = g_listMem[k];
            int eax = lsStart(hdr, k, n);
            bool ownStop = false;
            for (;;) {
                int next = eax + 1;
                if ((DWORD)next >= lsCount(hdr)) break;                            // list done
                eax = next;
                int r = lsStop(hdr, k, (DWORD)eax, n);
                if (r == 2) return;                                                // leave the call: epilogue
                if (r == 1) { ownStop = true; break; }                             // the engine's own stop: loop tail, no clean-up
                op(OP_MODULE, k, eax); g_fakeTsc += g_costModule + (int)(rnd() % 40); if (!(rnd() % 7)) ++g_vecPending;
            }
            if (!ownStop && lsDone(k, n)) op(OP_CLEANUP, k);
        }
    }
    int a = lsAfter(self, n);
    if (a == 2) return;
    if (a == 1) { op(OP_AI); g_fakeTsc += g_costAI; }
    op(OP_DESTROY, n, g_vecPending); g_vecPending = 0;
    if (n == 5) { op(OP_SUBS); g_fakeTsc += g_costSubs; g_subT[1] += g_costSubs; }
    op(OP_TAIL, n);
    if (n == 1) op(OP_TAIL1);
}
static void __fastcall modelUpdate(DWORD self, void* edx, DWORD n) {               // the entry stub + the body
    if (!g_lsInDriver && lsDrive(self, n)) return;
    body(self, n);
}
static void engineStep(DWORD self, int pattern) {                                  // the dispatcher: one step's calls
    DWORD lastN = pattern == 2 ? 2 + rnd() % 4 : 6;                                    // pattern 2: the engine abandons the step early (robustness only)
    for (DWORD n = 1; n <= lastN; ++n) {
        lsFrameBegin();
        unsigned long long t0 = g_fakeTsc;
        modelUpdate(self, NULL, n);
        lsFrameDone(n, g_fakeTsc - t0);
        if (pattern == 1 && (n & 1)) continue;                                     // two engine calls per render frame
        g_fakeTsc += 20000 + rnd() % 5000;                                         // the rest of the frame
    }
}
// A reference execution must not borrow an unfinished call's resume flags,
// pending destroy entries, clock or subsystem counters from the sliced run.
static void resetModel(DWORD self, bool sliced) {
    memset(g_fakeLogic, 0, sizeof(g_fakeLogic));
    *(DWORD*)(g_fakeLogic + 0x40) = 10;
    g_fakeTsc = 1000; g_vecPending = 0; g_lost = 0; g_ops.clear();
    g_lsWant = sliced ? 1 : 0; g_lsOn = sliced ? 1 : 0;
    g_lsInstalled = sliced ? 1 : 0; g_lsThis = self;
    g_lsStepFrame = 0; g_lsLastFrame = 10; g_lsNext = 7;
    g_lsPaused = g_lsAfterLists = g_lsInDriver = g_lsResume = 0;
    g_lsList = -1; g_lsIndex = 0; g_lsEngineN = 0; g_lsStepDriven = 0;
    g_lsT0 = g_lsBudget = g_lsStepTicks = g_lsSubsAtStep = 0;
    g_lsTotalEma = 10000; g_lsSubsEma = g_costSubs; g_lsFactor = 920;
    g_lsExpect = -1; g_lsMinTicks = 1000;
    for (int i = 0; i < 8; ++i) g_lsFrameTicks[i] = 0;
    for (int i = 0; i < SUB_N; ++i) g_subT[i] = 0;
}
static void dispatchRange(DWORD self, DWORD last) {
    for (DWORD n = 1; n <= last; ++n) {
        lsFrameBegin(); ULONG64 t0 = g_fakeTsc;
        modelUpdate(self, NULL, n);
        lsFrameDone(n, g_fakeTsc - t0);
        g_fakeTsc += 22000; // timing jitter must not consume simulation RNG
    }
}
static int interruptedSteps(DWORD self, unsigned seed) {
    long cases = 0, bad = 0, interrupted = 0;
    LONG catchesBefore = g_lsCatchUp, pausesBefore = g_lsPausesList;
    for (int round = 0; round < 400; ++round) {
        for (int k = 0; k < 4; ++k) {
            g_count[k] = 300 + (int)(rnd() % 600);
            g_listMem[k][0] = 0x1000; g_listMem[k][1] = 0x1000 + 4 * g_count[k];
        }
        g_costModule = 20 + (int)(rnd() % 120);
        g_costSubs = 2000 + (int)(rnd() % 30000);
        g_costN2 = (int)(rnd() % 12000); g_costAI = (int)(rnd() % 4000);
        for (DWORD cut = 2; cut <= 5; ++cut) {
            unsigned savedRng = g_rng;
            resetModel(self, false);
            for (int step = 0; step < 2; ++step)
                for (DWORD n = 1; n <= 6; ++n) body(self, n);
            std::vector<Op> ref = g_ops;
            resetModel(self, true); g_rng = savedRng;
            dispatchRange(self, cut);
            if (g_lsNext <= 6) ++interrupted;
            // The next n=1 first finishes the prior logical step. That work
            // belongs to the prior step, not a duplicate in the new step.
            // Keep list topology and destroy entries intact until it drains.
            dispatchRange(self, 6);
            ++cases;
            bool same = ref == g_ops;
            if (!same || g_lost || g_vecPending || g_lsNext != 7) {
                if (bad++ < 5) {
                    printf("INTERRUPTED cut %u case %ld: ref %u ops, actual %u, lost %d, pending %d, next %d\n",
                           cut, cases, (unsigned)ref.size(), (unsigned)g_ops.size(), g_lost, g_vecPending, (int)g_lsNext);
                    for (size_t i = 0; i < ref.size() && i < g_ops.size(); ++i)
                        if (!(ref[i] == g_ops[i])) { printf("  op %u: ref %d(%d,%d), actual %d(%d,%d)\n", (unsigned)i,
                            ref[i].kind, ref[i].a, ref[i].b, g_ops[i].kind, g_ops[i].a, g_ops[i].b); break; }
                }
            }
        }
    }
    LONG catches = g_lsCatchUp - catchesBefore, pauses = g_lsPausesList - pausesBefore;
    if (!interrupted || !catches || !pauses || g_lsSeqBad) ++bad;
    printf("seed %u: %ld interrupted-step cases, %ld unfinished, %d catch-ups, %d list pauses, %ld failures (full operation and destroy-payload comparison)\n",
           seed, cases, interrupted, (int)catches, (int)pauses, bad);
    return bad ? 1 : 0;
}
int main(int argc, char** argv) {
    if (argc > 1) g_rng = (unsigned)atoi(argv[1]);
    DWORD self = (DWORD)(ULONG_PTR)g_fakeLogic;
    if (argc > 2 && atoi(argv[2]) == 2) return interruptedSteps(self, g_rng);
    g_subGlob[0] = 1; g_lsInstalled = 1; g_lsMinTicks = 1000;                     // the model's clock is in arbitrary units
    long steps = 0, bad = 0, sliced = 0; long long mods = 0;
    for (int round = 0; round < 400; ++round) {
        for (int k = 0; k < 4; ++k) { g_count[k] = (int)(rnd() % (k == 0 ? 900 : 300)); if (!(rnd() % 9)) g_count[k] = (int)(rnd() % 3); g_listMem[k][0] = 0x1000; g_listMem[k][1] = 0x1000 + 4 * g_count[k]; }
        g_costModule = 20 + (int)(rnd() % 120); g_costSubs = 2000 + (int)(rnd() % 30000); g_costN2 = (int)(rnd() % 12000); g_costAI = (int)(rnd() % 4000);
        int pattern = (rnd() % 5) == 0 ? 1 : 0;
        if (argc > 2 && atoi(argv[2]) == 2 && !(rnd() % 3)) pattern = 2;
        for (int s = 0; s < 6; ++s) {
            if (!(rnd() % 3)) { int k = (int)(rnd() % 4); g_count[k] += (int)(rnd() % 20); g_listMem[k][1] = 0x1000 + 4 * g_count[k]; }   // lists grow between steps (appends)
            // reference: the stock sequence for this state
            unsigned savedRng = g_rng; DWORD savedFrame = *(DWORD*)(g_fakeLogic + 0x40);
            LONG wantSaved = g_lsWant; g_lsWant = 0;
            // (run the reference on a copy of the slicer state that declines everything)
            LONG on = g_lsOn; g_lsOn = 0; g_ops.clear(); g_vecPending = 0;
            { LONG inst = g_lsInstalled; g_lsInstalled = 0;                          // reference run: driver off, no measuring
              for (DWORD n = 1; n <= 6; ++n) { g_lsOn = 0; if (n == 1) { /* lsDrive would flip g_lsOn at n==1: bypass the driver entirely */ } body(self, n); }
              g_lsInstalled = inst; }
            std::vector<Op> ref = g_ops;
            g_rng = savedRng; *(DWORD*)(g_fakeLogic + 0x40) = savedFrame; g_lsWant = wantSaved; g_lsOn = on;
            g_ops.clear(); g_vecPending = 0; g_lost = 0;
            engineStep(self, pattern);
            ++steps; if (g_lsStepDriven) ++sliced;
            // the module-cost jitter uses the same rng stream in both runs only if the op order is the same: compare kinds and indices (not the destroy payload, which depends on that jitter)
            bool same = ref.size() == g_ops.size();
            for (size_t i = 0; same && i < ref.size(); ++i) same = ref[i].kind == g_ops[i].kind && ref[i].a == g_ops[i].a && (ref[i].kind == OP_DESTROY || ref[i].b == g_ops[i].b);
            if (pattern == 2) {                                                        // no stock equivalent: every module at most once, nothing lost, no hang
                std::vector<char> seen[4]; for (int k = 0; k < 4; ++k) seen[k].assign(g_count[k] + 64, 0);
                for (size_t i = 0; i < g_ops.size(); ++i) if (g_ops[i].kind == OP_MODULE) { char& c = seen[g_ops[i].a][g_ops[i].b]; if (c) { if (bad++ < 5) printf("STEP %ld: module %d/%d twice\n", steps, g_ops[i].a, g_ops[i].b); } c = 1; }
                if (g_lost) { if (bad++ < 5) printf("STEP %ld: lost vector entries %d\n", steps, g_lost); }
            } else if (!same || g_lost) {
                if (bad++ < 5) { printf("STEP %ld differs (ref %u ops, sliced %u ops, lost vector entries %d)\n", steps, (unsigned)ref.size(), (unsigned)g_ops.size(), g_lost);
                    for (size_t i = 0; i < ref.size() && i < g_ops.size(); ++i) if (!(ref[i].kind == g_ops[i].kind && ref[i].a == g_ops[i].a)) { printf("  first difference at op %u: ref %d(%d,%d) sliced %d(%d,%d)\n", (unsigned)i, ref[i].kind, ref[i].a, ref[i].b, g_ops[i].kind, g_ops[i].a, g_ops[i].b); break; } }
            }
            for (size_t i = 0; i < g_ops.size(); ++i) if (g_ops[i].kind == OP_MODULE) ++mods;
        }
        if (!(rnd() % 6)) { *(DWORD*)(g_fakeLogic + 0x40) = 0; }                       // a new match: the frame counter starts over
    }
    printf("seed %u: %ld steps (%ld driven by the slicer), %lld module updates, steps that differ from the stock order: %ld | leaves in a list %d, before the n5 block %d, catch-ups %d, odd %d, order self-check %d/%d bad\n",
           argc > 1 ? (unsigned)atoi(argv[1]) : 777u, steps, sliced, mods, bad, (int)g_lsPausesList, (int)g_lsPausesBlock, (int)g_lsCatchUp, (int)g_lsOdd, (int)g_lsSeqBad, (int)g_lsSeqChecked);
    return bad ? 1 : 0;
}
