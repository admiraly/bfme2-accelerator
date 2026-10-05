# BFME II optimization track

This fork is the implementation home for extending the accelerator to BFME II.
Open-BFME-1 and Open-BFME-2 supply reference evidence; the seven verified BFME II 1.06 bindings are enabled by default. Other
engine-specific hooks remain disabled until their target addresses, ABI and
touched layouts are verified. Set the relevant `BFME2_*` variable to `0` to
disable a verified binding; no environment setup is needed to enable it.

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
| Audio limit loop has one exact hit | 54 bytes at VA `0x004576CB`; donor VA `0x00456E88` | Guarded default audio binding; runtime validation pending |

Open-BFME-2's matched ledger also supplies source leads for:

| Function | BFME II RVA | Next investigation |
| --- | --- | --- |
| `ThingTemplate::isEquivalentTo` | `0x0033BB04` | Mutation/lifetime rules and memoization invalidation |
| `MutexClass::Lock` | `0x00613A30` | Named mutexes, timeouts, ownership, object lifetime |
| `HLodClass::Update_Sub_Object_Transforms` | `0x0019D640` | Layout, sub-object ownership, floating-point state and thread safety |

These are matched-ledger identities, not accelerator replacements. RVAs are
relative to the image base; the audio-loop address above is a VA.

## Large Address Aware copy

The pinned vanilla 1.06 image has PE Characteristics `0x010F`, without LAA.
`tools/bfme2_laa.py` creates a separate copy with Characteristics `0x012F`.
Only bit `0x20` of the byte at file offset `0x136` changes. Code, imports,
resources, image base and accelerator `.text` recognition remain identical.

Python 3, from the checkout or the packaged development build:

```bat
python tools/bfme2_laa.py "C:\Games\BFME II\game.dat"
python tools/bfme2_laa.py "C:\Games\BFME II\game.dat" --output "C:\Games\BFME II\game.laa.dat"
```

The first command only inspects and reports the proposed change. The second
creates and verifies the new copy. Only the exact pinned baseline and its
LAA-only variant are accepted. Other builds or modifications are refused;
existing outputs and the original path are never overwritten. An already-LAA
input produces an identical separate copy.

Close the game before installing the copy. Keep the original as `game.dat.pre-laa`,
then rename `game.laa.dat` to `game.dat` in the installation. Restart through the
usual loader. To undo, restore `game.dat.pre-laa` as `game.dat`. Merely generating
the copy does not change which executable the game loader launches; changing the
flag in an already-running process would not change its launch-time address limit.

On 64-bit Windows, LAA increases the x86 user virtual address-space ceiling from
2 GB to 4 GB; it does not make the engine 64-bit or reduce memory consumption.
See [Microsoft's address-space limits](https://learn.microsoft.com/en-us/windows/win32/memory/memory-limits-for-windows-releases).
In-game behavior with allocations above 2 GB remains unvalidated. The allocator,
engine and third-party DLLs may still have high-address assumptions.

Patched-copy SHA256:
`5de725b7e396400d48e1b103252756bd32a84cf014ee093f072fc5fc6f5e0f43`.
CI checks the exact one-bit change, original preservation, refusal paths,
idempotence and unchanged accelerator recognition on both Linux and Windows.
Two x86 Windows probes, linked with and without LAA, also check actual OS limits
and whether a page at address `0x90000000` can be allocated, written and read.
These probes do not run the game. No game executable is included in build artifacts.

## Experimental BFME II audio index

The 1.06 profile now binds the existing audio index independently of the global
engine-hook flag. Its processing pass is VA `0x0046258D`; shared push-back,
push-front, erase, pop-back and clear methods are guarded and detoured at entry.
The mutation callbacks track the audio manager's list at offset `0x98`. The
original list methods remain callable through instruction-aligned trampolines.
The count loop preserves both stock counters and the original continuation.

Launch the default vanilla 1.06 build from a Command Prompt:

```bat
bfme2_accel_loader.exe
```

It is enabled by default and applies only to the recognized BFME II 1.06 text hash.
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
long sessions, loading/saving, replays and multiplayer when evaluating the enabled defaults.

## Experimental native BFME II optimizations

Two further bindings are available independently of the global engine-hook flag:

```bat
bfme2_accel_loader.exe
```

Both are enabled by default and restricted to the recognized vanilla 1.06 build
loaded at `0x00400000`.
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
prologues, the 8,192/8,193-record boundary, unknown-comparator stock delegation,
opt-out, guard/allocation/write failures and deliberate wrong-oracle
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
comparisons retain the original CRT behavior; non-C locales delegate the entire
comparison, including ASCII bytes, to the original CRT. The exported VS2003
`__lc_handle[LC_CTYPE]` value is read on each call, so live locale changes are
observed without cached locale assumptions. A missing export also delegates
these comparisons. Loads stay inside readable
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

## Experimental native string comparison

The default string optimization binds `StringBase::compareNoCase` at VA `0x00406A00`
for vanilla 1.06. It reads each current header, its unsigned 16-bit length at
`+4`, and its bytes at `+8` directly, replacing two wrapper calls before the
bounded CRT comparison. Null headers represent empty strings. There is no
memoization, allocation or header mutation. The exact result is the bounded
comparison result, or the signed length difference when that comparison is zero.
The SIMD path applies only to the C locale; non-C locales and non-ASCII bytes
use the original CRT locale path. Two objects sharing the same
current header return equality without scanning that header; the sampled stock
checks still apply. This identity check retains no pointer cache.

The complete 42-byte entry and 44-byte bounded helper at `0x00405841` are guarded.
The entry trampoline copies six complete instruction bytes. The recognized
image hash, fixed image base and resolved original CRT comparison are required.
Allocation, write or guard failure leaves the feature off. The first 20,000
comparisons per thread and one in 64 thereafter compare against the original
member routine; a difference retains the original return and disables the hook.
This option is independent of the other experimental bindings.

The first implementation, revision `09970a7d6b94fd9d9703cdaceff8ab8a8ac49fbb`,
passed 4,645,728 exact bounded-helper comparisons and 500,025 native-header
comparisons against the original machine code. The current oracle also injects
shared headers, including non-ASCII data. Cases cover null headers,
embedded terminators, non-ASCII bytes, storage reuse, all byte pairs at every
SIMD lane, protected-page boundaries and the full 16-bit length range.
Best-of-five equal-ASCII member timings, with sampled stock checks included:

| Length | Untouched member | SIMD import path | Direct member hook | SIMD import / direct |
| --- | --- | --- | --- | --- |
| 8 | 33.66 ns | 22.68 ns | 20.50 ns | 1.11x |
| 16 | 70.28 ns | 11.40 ns | 6.73 ns | 1.69x |
| 32 | 146.14 ns | 13.17 ns | 9.29 ns | 1.42x |
| 64 | 294.73 ns | 16.47 ns | 13.60 ns | 1.21x |
| 128 | 604.78 ns | 23.74 ns | 22.62 ns | 1.05x |

The subsequent short-span change uses 8- and 4-byte SSE2 loads when fewer than
16 bytes remain. Each load remains within the requested count and both pages;
upper unused lanes are masked out. The full-width branch stays ahead of narrow
handling, and a scalar first-byte check resolves short mismatches before narrow
loads. A shared event resolver keeps the vector hot path compact. A test-only
snapshot benchmarks the previous 16-byte-only implementation alongside the new
one, including early mismatches.

On revision `9851122e6c319e5cfbcc4e10a35121ff58994dbe`, a comparison
benchmark, before the additional per-call locale gate, produced the following
representative results against the previous 16-byte-only SIMD implementation:

| Bounded span | Previous | New | Previous / new |
| --- | --- | --- | --- |
| Equal 4 bytes | 10.47 ns | 5.27 ns | 1.99x |
| Equal 8 bytes | 18.64 ns | 5.09 ns | 3.66x |
| Equal 12 bytes | 29.12 ns | 9.19 ns | 3.17x |
| Equal 24 bytes | 21.65 ns | 7.66 ns | 2.83x |

Early-mismatch timings were 1.02–1.06x faster; equal full-width spans ranged
from 0.97–1.03x. Native direct-member comparisons were 1.10–1.55x faster than
the current SIMD import path for equal 8–128-byte spans, and 1.34–1.53x faster
for early mismatches. Shared-header comparisons were 4.21–7.96x faster than
that import path. These include sampled stock checks. The single-range ASCII
fold also measured 1.00–1.06x versus the previous vector fold in unbounded
comparisons. Such small differences are sensitive to hosted-runner noise.
These are function measurements, not battle or FPS gains. The final artifact
also includes the per-call locale gate and live English, Turkish, German and C
locale checks; its timings supersede these earlier measurements. Turkish tests
try language-only and old/new country spellings for current Windows NLS data.

Installed-entry tests exercise the real copied-prologue trampoline, null headers,
allocation/write/guard failures, injected oracle mismatches and 20,000 populated
list cases with both the string and equivalence detours active. The independent
reference temporarily disables the string hook. These isolated native-code checks
do not establish in-game threading, save/replay compatibility or battle FPS.

## Next implementation steps

1. Validate the default audio binding in a running game. Profile indexed and
   stock runs of the same replay, review mismatch and mutation counters, and
   audit additional mutations and audio manager lifetime paths.
2. Profile a reproducible BFME II battle with portable features enabled and
   disabled. Choose the next feature from the measured remaining costs.
3. Validate the native string, sort and equivalence bindings in-game. Port filter caches
   and locks only after their invalidation and lifetime evidence is complete.
4. Investigate pose workers and logic spreading after their dependency maps
   are complete. Preserve floating-point state, operation order and object
   lifetime; validate rendering interactions.
5. Test saves, replays and two-client multiplayer when evaluating the enabled defaults.

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


## Default network packet CPU optimization

The default network optimization replaces the active inline packet scrambling blocks in
vanilla 1.06, independently derived from the pinned machine code. The unused
standalone encode/decode helpers remain intact as native runtime oracles.

| Site | Address | Role |
| --- | --- | --- |
| Queue-send function | `0x008D4EC6` | Guard all 180 bytes |
| Send block | `0x008D4F47` → `0x008D4F75` | Store the existing CRC, transform payload plus CRC |
| Receive function | `0x008D4D08` | Guard all 446 bytes |
| Receive block | `0x008D4D87` → `0x008D4DBF` | Transform received datagram before existing CRC checks |
| Native encode/decode helpers | `0x008D4A8E` / `0x008D4AC0` | Guard all 100 bytes; private ECX-buffer/EAX-length ABI |

Each full 32-bit word uses a wrapping key starting at `0x38D9B7D4`, subtracting
`0x7F39C50E` per word. Encoding byte-swaps the word after XOR; decoding does
so before XOR. The native loops call Winsock `htonl` for every word. The new
implementation handles four words per SSE2 operation, with inline scalar tails.
Trailing one to three bytes remain untouched, matching native behavior. There
are no allocations in packet processing and no change to wire bytes, checksums,
packet counts, command ordering, socket operations, retries or frame admission.

The two six-byte detours replace complete instructions. The send continuation
returns AL=1 and restores its nonvolatile registers. The receive continuation
has no live loop scratch slots or flags; ESI, EDI and EBP remain intact. Enclosing
function guards also cover those continuations. Each site is independent: a
refused write can leave that direction stock without requiring rollback.

Send packets shorter than 36 bytes and receive packets shorter than 16 bytes
use fully unrolled scalar XOR/BSWAP instructions
in the detour itself, avoiding C calls and SIMD setup. These short paths are
covered by native inline-block tests at every length and alignment. They check
the global disable flag but do not perform runtime shadow comparisons.

Runtime proof compares the first 20,000 eligible vector calls per direction per thread,
then one in 64, against the unmodified native helper. A byte mismatch restores
the native packet and disables both directions. Lengths below four or above the
native 1038-byte receive bound use stock code. Shadow copies use a separate
non-inlined function so the unchecked path does not allocate the 1038-byte stack
buffer. Proof sampling can initially cost more than stock processing.

Linux checks compare every size through 1100 bytes and all 32 alignments to an
independent byte-wise oracle. Windows tests additionally execute the mapped
native helpers and the real inline blocks before and after detouring, check
protected-page boundaries and every guarded-byte mutation, and deliberately
inject an incorrect oracle to verify restoration and disable behavior. Timings
measure synthetic packet CPU work including runtime sampling; they do not
measure multiplayer latency or overall frame rate. Network sessions and
installation-time thread concurrency remain untested. The user requested default activation; these in-game checks remain outstanding.

Frame waits depend on command availability and the existing native pacing.
Reducing packet CPU work cannot remove the wait for another player's commands.
Changing delay, retry, buffering or frame-rate rules requires a separate
compatibility investigation and two-client measurements.

The first Windows run passed 344,687 native packet comparisons and all 726
individual guard-byte mutations. Its checked-transform microbenchmark measured
about 1.9–8.3x versus the original helper at 16–1024 bytes (roughly 4–51 ns
versus 9–421 ns on that shared runner). Those figures exclude the active inline
bridge; a separate before/after inline-block benchmark reports the complete
detour cost. These are small absolute savings and do not establish a measurable
multiplayer or frame-rate improvement. Independent write-refusal checks verify
that either direction can remain stock while the other installs.

The complete inline benchmark exposed an initial 16-byte send regression
(10.66 ns native versus 12.87 ns through the cdecl bridge), despite larger-packet
speedups. The hook now passes buffer and length in ECX/EDX using a fastcall
wrapper, removing argument pushes and caller stack cleanup. Small packets use an unrolled scalar path within the hook to avoid that overhead
completely. On sends it also avoids writing only the four-byte CRC immediately
before loading a 16-byte vector from the same address. Native private-ABI
oracle calls retain their original ECX/EAX convention. Recheck the inline timing
rows when evaluating this build; helper-only timings do not prove a faster hook.


## Default exact packet and RNG hash

`BFME2_CRCFAST=0` disables the default binding at VA `0x007EC8F7`.
This native helper computes rotate-left-by-one plus each unsigned input byte,
with 32-bit wrapping after every byte. It is not IEEE CRC32 or CRC32C. The
independently implemented replacement folds eight bytes with a weighted sum and
one end-around carry when a conservative bit guard proves that no native
addition can wrap. The sum is at most 65025. If initial bits 15..23 are all one,
it retains the exact eight-step recurrence; this covers every potentially
overflowing rotated input because each dangerous value has its high 16 bits
set. The fallback preserves carry behavior and the distinct all-ones/zero hash
representations. Computation uses integer operations, retaining native XMM and
floating-point controls, which the native-entry tests explicitly check.
Null input returns the initial hash even for a nonzero length; an empty range
is never read. The function writes neither input bytes nor RNG state.

The entire 43-byte helper is guarded, including RET (the reconstruction comment
counts 42 bytes and omits that final instruction). The six-byte stolen prologue
contains only whole instructions. A trampoline preserves the original cdecl
oracle; the first 20,000 eligible calls per thread and one in 64 thereafter are
compared with stock. Mismatches retain stock hashes and disable the replacement.
Allocation, guard and write failures leave the original entry intact.

Tests compare an independent carry/add oracle on Linux and the actual mapped
native helper on Windows. They cover arbitrary initial hashes, all byte values,
alignments, null/empty inputs and protected-page ends. Windows also exercises
the original RNG checksum getter at `0x00633F70` against its six-word state at
`0x00DBA3D0`, checking both the hash and unchanged seed bytes after installation.
Complete entry-point benchmarks include detour and sampled-proof overhead.
These tests establish function equivalence, not full multiplayer determinism or
an overall lag/FPS gain; live sessions remain untested.


Tiny hashes (up to eight bytes), empty ranges and null buffers use an integer
assembly path in the entry hook to avoid C/TLS setup. They honor the disable
flag and are covered by native comparisons, but do not perform runtime shadow
checks. Larger hashes retain sampled proof. The initial unrolled-only candidate
was slower for tiny inputs and close to parity for large buffers; complete-hook
benchmarks, rather than helper-only results, drove these revisions.

## Memory moves and floating-point determinism audit

Forward-overlapping memmove ranges now use increasing SSE2 blocks and exact-width
tails instead of REP MOVSB. Backward moves retain decreasing blocks and replace
byte-wise tails with 16/8/4/2/1-byte chunks. For 16..32 bytes, both overlapping
source blocks are loaded before either store. No tail is re-read after stores.
Tests compare the whole destination buffer and return pointer with the pinned
CRT, across both directions, small displacement, alignments, larger ranges and
protected-page ends. Timings compare native CRT, the previous accelerator and
the replacement; they do not establish a frame-rate improvement.

A new 105,984-case floating-point matrix checked x87 precision/rounding and
MXCSR rounding, DAZ, FTZ and pre-existing exception flags. Values and control
settings matched native floor/ceil in every case. There were 26,894 differences
in exception-status side effects. Those are diagnostic findings, not a confirmed
source of game desync; engine consumption of those flags needs further tracing.
No floating-point mode is forcibly changed by this pass.


## Default IEEE CRC32 and memory-copy dispatch

The separate native IEEE CRC32 helper at `0x00A19AC0` is replaced by slicing-by-8
and a slicing-by-4 tail. It preserves the original seed complement, polynomial
`0xEDB88320`, final complement and exact range boundaries. Its entire 51-byte
body and native 1024-byte table at `0x00DD5F38` must match before installation
and again with threads suspended. A five-byte trampoline steals the four-byte
argument load and one-byte PUSH only. Derived tables consume 8 KiB and are built
once during installation; no per-call allocation occurs. `BFME2_CRC32FAST=0`
disables it. Tiny hashes (<=8 bytes) bypass runtime proof; larger ones compare
the first 20,000 calls per thread and one in 64 afterward. Mismatches return stock
and disable the binding. Null is accepted only with length zero, as in native.

Independent bit-wise oracle tests and the standard `123456789` check vector
verify the polynomial. Native tests include arbitrary initial hashes, guarded
pages, every code/table guard mutation, real detours, XMM/control preservation,
write/allocation refusal and injected incorrect-oracle fallback. This is not the
rotate/add packet hash; hardware CRC32C instructions would compute a different
checksum and are not substituted.

Complete memmove benchmarks identified an important exception to the SSE2 path:
ERMS REP MOVSB wins for some wider overlaps. CPUID dispatch retains it for forward
moves with distance >=64 bytes and length >=1024, and for 33..256-byte moves with
distance >=8. Other forward overlaps use the ordered vector path. Timings on the
shared runner are useful for selecting these paths but are not a guarantee of
optimal thresholds on every processor.

The observed game `_statusfp` import call is at `0x00440EAF`, immediately after
`_fpreset` at `0x00440EA9`; the following `_controlfp` operation uses mask
`0x30300`. No other direct calls to those three IAT slots were found in the
pinned text. That path therefore does not establish the measured floor/ceil
status-flag differences as a desync mechanism. Indirect consumers and live game
behavior remain outside this static audit. Frame-admission, logic rate, retries
and the structurally mapped community delay patch remain unchanged because
end-to-end timing compatibility is not established by function-level tests.
