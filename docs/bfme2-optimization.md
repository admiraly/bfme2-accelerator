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
| Audio limit loop has one exact hit | 54 bytes at VA `0x004576CB`; donor VA `0x00456E88` | A promising relocation lead, not a verified audio port |

Open-BFME-2's matched ledger also supplies source leads for:

| Function | BFME II RVA | Next investigation |
| --- | --- | --- |
| `ThingTemplate::isEquivalentTo` | `0x0033BB04` | Mutation/lifetime rules and memoization invalidation |
| `MutexClass::Lock` | `0x00613A30` | Named mutexes, timeouts, ownership, object lifetime |
| `HLodClass::Update_Sub_Object_Transforms` | `0x0019D640` | Layout, sub-object ownership, floating-point state and thread safety |

These are matched-ledger identities, not accelerator replacements. RVAs are
relative to the image base; the audio-loop address above is a VA.

## Next implementation steps

1. Audio indexing: identify the enclosing limit function and processing pass;
   prove the list and event layouts; locate every list mutation and verify
   call destinations. Port the guards before the replacement. Keep shadow
   comparisons and automatic disable on differences.
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

The first CI execution exposed a missing `aotrPath` helper in the standalone
logic harness; the harness now supplies it. The expanded test also reports
failures in its experimental abandoned-step mode (`logicslicer_test SEED 2`).
That mode mixes interrupted steps, catch-up work and changing list sizes; its
model and the driver both need investigation before attributing a game bug.
The robustness check remains a failing CI gate, after artifact upload, rather
than being suppressed or treated as multiplayer validation.

The CRT test is compiled but needs the original `msvcr71.dll` to run. The sort
test is compiled but currently expects the donor RotWK addresses. Render
equivalence needs D3D9/D3DX and an appropriate graphics environment. No battle
benchmark, multiplayer validation or speedup claim is supplied by this CI.
