# BFME II optimization track

This fork is the implementation home for extending the accelerator to BFME II.
Open-BFME-1 and Open-BFME-2 supply reference evidence; engine-specific hooks
remain disabled until their target addresses, ABI and touched layouts are verified.

## Reproduce the first preflight

Python 3, with no third-party packages:

```sh
python tools/accelerator_audit.py --image /path/to/BFME2/game.dat
```

The audit reads the executable, reproduces the accelerator's `.text` FNV-1a
fingerprint, inspects imported DLL identities, reads the current build table,
and searches for the audio-limit guard's exact loop bytes. It never patches
the executable or authorizes a hook. Malformed inputs return exit code 2.
Unknown or changed valid executables produce a report rather than being
misidentified as the repository baseline.

## Established static findings (2026-10-05)

Reference: Open-BFME-2 revision `bdc65d655b4f326e7a66939af2975b757a3098a1`,
`baselines/bfme2/workshop-vanilla-1.06/files/game.dat`.
Accelerator donor: `76ccf2dc1fac0baf644987efea6ad460f8cb01d2`.

| Observation | Evidence | Meaning |
| --- | --- | --- |
| Exact vanilla 1.06 baseline | SHA256 `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640` | Reproducible starting image |
| Accelerator recognizes this build | `.text` hash `0x32667B9B` | Existing BFME2 build-table entry applies |
| Engine hooks disabled | BFME2 entry has `engineHooks = false` | Portable features only; runtime prerequisites still apply |
| Required family imports present | `msvcr71.dll`, `mss32.dll`; image base `0x00400000` | Static family prerequisites, not proof of loaded DLLs |
| Audio limit loop has one exact hit | 54 bytes at VA `0x004576CB`; donor VA `0x00456E88` | Guarded opt-in audio binding; runtime validation pending |

Open-BFME-2's matched ledger also supplies source leads for:

| Function | BFME II RVA | Next investigation |
| --- | --- | --- |
| `ThingTemplate::isEquivalentTo` | `0x0033BB04` | Mutation/lifetime rules and memoization invalidation |
| `MutexClass::Lock` | `0x00613A30` | Named mutexes, timeouts, ownership, object lifetime |
| `HLodClass::Update_Sub_Object_Transforms` | `0x0019D640` | Layout, sub-object ownership, floating-point state and thread safety |

These are matched-ledger identities, not accelerator replacements. RVAs are
relative to the image base; the audio-loop address above is a VA.

## Experimental BFME II audio index

The 1.06 profile now binds the existing audio index independently of the global
engine-hook flag. Its processing pass is VA `0x0046258D`; shared push-back,
push-front, erase, pop-back and clear methods are guarded and detoured at entry.
The mutation callbacks track the audio manager's list at offset `0x98`. The
original list methods remain callable through instruction-aligned trampolines.
The count loop preserves both stock counters and the original continuation.

Enable it for a controlled vanilla 1.06 test from a Command Prompt:

```bat
set BFME2_AUDIOINDEX=1
bfme2_accel_loader.exe
```

It is off by default and applies only to the recognized BFME II 1.06 text hash.
All eight byte guards must match; all seven patch sites must be writable before
any code changes. Guards are checked again with other threads suspended. A
changed guard or preflight failure leaves the feature off. `AOTR_AUDIOLIMIT=0`
also disables it. Look for `bfme2-audio` in the accelerator log.

The existing index checks its first 20,000 indexed answers against the stock
walk, then checks one in 64. A difference uses the stock answer and disables
indexing. Small lists, rebuild limits and out-of-pass calls use the stock walk.
These comparisons do not prove that unobserved mutations are impossible.
Shared-list detours can add overhead outside the audio manager; a battle
benchmark must measure both their cost and the saved traversal work.

This is an experimental binding, not a measured speedup. Validate battle audio,
long sessions, loading/saving, replays and multiplayer before changing defaults.

## Experimental native BFME II optimizations

Two further bindings are available independently of the global engine-hook flag:

```bat
set BFME2_EQUIVFAST=1
set BFME2_RLSORT=1
bfme2_accel_loader.exe
```

Both are off by default and restricted to the recognized vanilla 1.06 build.
The equivalence binding guards the complete 335-byte routine at VA `0x0073BB04`
and its 14-byte final-override getter. It answers null, identity and empty-list
cases directly; populated lists stay stock. It reads current original-template
list bounds and override chains on every call, retaining no cached addresses or
answers. The first 20,000 fast answers per thread and then one in 64 are checked
against stock; a difference returns stock and disables the feature globally.

The sort binding hooks only VA `0x00574870`. Its byte guards cover the complete
helper region `0x0057330A..0x005748B2` and 89-byte median helper at `0x008C6F77`.
It uses the existing accelerator algorithm with BFME II's two verified mesh
comparators and heap fallback. Other comparators and lists over 8,192 records
stay stock. Record moves preserve all 32 bytes without reference-count updates.
The first 32 eligible sorts per thread and then one in 256 are compared with
stock order; a difference restores stock order and disables the replacement.
Donor push, erase and flush hooks are not enabled by this binding.

Windows CI maps the pinned game image in a suspended test child. It starts no
game entry point, constructors, gameplay or graphics. The tests execute the
original equivalence, sort and helper machine code; for populated equivalence
checks only the required `_strnicmp` import is resolved to the pinned CRT.

Validation includes 2,112,500 equivalence cases with overrides, populated lists,
nulls and storage reuse; 3,240 sorting cases comparing bytes, comparator sequences
and final reference counts; forced heap fallback; and rejection of 349 equivalence
and 5,634 sort guard mutations. Tests also exercise real entry detours and copied
prologues, opt-out, guard/allocation/write failures and deliberate wrong-oracle
mismatch fallback. Thread suspension in this isolated child test is a no-op;
in-game concurrent patch installation remains unvalidated.

Microbenchmarks on commit `a98e88eb5816c62f4bd1150bea62f1ddefd6ed7f`:

| Workload | Stock | Full hook | Stock / hook |
| --- | --- | --- | --- |
| Empty template lists, no overrides | 3.37 ns | 3.48 ns | 0.97x |
| Empty template lists, 4 overrides | 8.31 ns | 5.62 ns | 1.48x |
| Empty template lists, 8 overrides | 10.12 ns | 7.57 ns | 1.34x |
| Sort 64 mesh records | 12.33 us | 0.43 us | 28.41x |
| Sort 256 mesh records | 49.38 us | 1.70 us | 28.99x |
| Sort 1,024 mesh records | 256.76 us | 8.48 us | 30.27x |

Sorting timings include restoring the input copy each iteration and the hook's
sampled stock-order checks. Data uses fake meshes with valid measured field
layouts and reference counts. Best-of-five alternating measurement order on a
shared Actions runner does not represent an actual battle. Equivalence is not a
uniform win: its no-override case was slightly slower, so it must be evaluated
with the actual workload before enabling. None of these ratios is an FPS claim.

## Remaining engine candidates

Object-filter caching needs the target routine and static/dynamic split, plus
verified invalidation for filter and template reloads. Its donor addresses and
frame-reset heuristic are insufficient to bind it to BFME II.

The target mutex lock has a reconstruction lead, but unlock is marked
present-unmatched in the reference, and replacing kernel mutexes requires proofs
for named handles, recursion, timeouts, abandonment, ownership and destruction.
The existing donor adoption mechanism is not grounds to enable target locks.

Skeleton-transform source and layout leads exist, but safe worker offload also
requires animation mutation ownership, render-pass dependency order, cleanup,
floating-point state and device-reset interactions. A synthetic sort oracle
cannot establish these rendering and concurrency properties.

These features remain disabled. The next larger port should follow a battle
profile, save/replay checks and two-client multiplayer validation.

## Further performance changes

Case-insensitive ASCII comparisons now process 16 bytes with SSE2. The first
mismatch, terminator or high-byte event is resolved in byte order. Non-ASCII
comparisons retain the original CRT locale behavior; loads stay inside readable
pages, and bounded comparisons do not load beyond their requested count. This
uses the existing import replacement path and does not need engine addresses.

An Actions measurement of equal ASCII strings on revision
`2a2a55ba2180eab2fbc0f5e05d09ecb18bde55d7` produced:

| Length | Previous scalar | SIMD | Previous / SIMD |
| --- | --- | --- | --- |
| 8 | 12.46 ns | 3.79 ns | 3.29x |
| 16 | 22.73 ns | 5.41 ns | 4.20x |
| 32 | 43.58 ns | 7.04 ns | 6.19x |
| 64 | 85.89 ns | 10.29 ns | 8.35x |
| 128 | 169.19 ns | 16.80 ns | 10.07x |
| 256 | 341.26 ns | 29.84 ns | 11.44x |

These are best-of-five function timings on a shared hosted runner, with alternating
measurement order. They do not establish whole-game performance or the workload's
mix of equal, unequal and non-ASCII names. Results are uploaded as an artifact;
performance ratios are informational and never a CI acceptance threshold.

Production audio builds now omit timestamp reads and diagnostic-only volatile
counter writes. Mutation tracking, fault limits, mismatch disable, the initial
20,000 stock comparisons and the subsequent one-in-64 cadence remain active.
The production audio harness compares answers directly and checks that telemetry
stays zero while proof comparisons complete. Use the diagnostic DLL when counters
are needed; use the production DLL for battle performance measurements.

## Next implementation steps

1. Validate the opt-in audio binding in a running game. Profile indexed and
   stock runs of the same replay, review mismatch and mutation counters, and
   audit additional mutations and audio manager lifetime paths.
2. Profile a reproducible BFME II battle with portable features enabled and
   disabled. Choose the next feature from the measured remaining costs.
3. Validate the native sort and equivalence bindings in-game. Port filter caches
   and locks only after their invalidation and lifetime evidence is complete.
4. Investigate pose workers and logic spreading after their dependency maps
   are complete. Preserve floating-point state, operation order and object
   lifetime; validate rendering interactions.
5. Test saves, replays and two-client multiplayer before changing defaults.

Do not set BFME2's global `engineHooks` flag to true: that would enable donor
addresses across several unrelated subsystems. Introduce verified per-build,
per-feature support instead.

## GitHub Actions coverage

The workflow builds x86 debug and production DLLs, the placeholder-art launcher,
injector and offline harnesses on Windows. Audio indexing and logic sequence
checks run with several seeds. A Linux job audits the pinned reference binary.
Development artifacts expire after 14 days and are not releases.

The interrupted-step regression isolates stock and sliced model state, compares
complete operation sequences including destruction payloads, and exercises all
four interruption points across 400 randomized cases per seed. All 4,800 cases
pass across three seeds. The former failures were caused by mixing previous-step
catch-up work into the new step's duplicate count and sharing reference state.
No scheduler implementation change was needed; multiplayer remains untested.

The BFME II audio guard test runs the runtime guard routine against the pinned
mapped image, then changes every guarded byte individually and requires rejection.
Neither test executes the game binary.

The CRT test runs against the pinned VS2003 `msvcr71.dll` from Open-BFME-1
revision `c0409ae46dd306661857c38c5542dc2f4608e7dc`, file
`build/toolchains/vs2003/msvcr71.dll` (Git blob
`9d9e0286c47f2e63f2ab89960332a85204f484ef`). The SIMD case test adds
6,891,456 exact-return cases, every byte pair at every SIMD lane and
protected-page checks. An installed game's CRT may differ; this pinned toolchain
reference does not establish that every installation uses that exact DLL. The sort
test is compiled but currently expects the donor RotWK addresses. Render
equivalence needs D3D9/D3DX and an appropriate graphics environment. No battle
benchmark, multiplayer validation or speedup claim is supplied by this CI.
