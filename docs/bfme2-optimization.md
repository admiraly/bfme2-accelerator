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

## Next implementation steps

1. Validate the opt-in audio binding in a running game. Profile indexed and
   stock runs of the same replay, review mismatch and mutation counters, and
   audit additional mutations and audio manager lifetime paths.
2. Profile a reproducible BFME II battle with portable features enabled and
   disabled. Choose the next feature from the measured remaining costs.
3. Port sorting, equivalence caching and locks independently where evidence
   supports them. Audit mutation and cleanup paths as well as steady state.
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

The CRT test is compiled but needs the original `msvcr71.dll` to run. The sort
test is compiled but currently expects the donor RotWK addresses. Render
equivalence needs D3D9/D3DX and an appropriate graphics environment. No battle
benchmark, multiplayer validation or speedup claim is supplied by this CI.
