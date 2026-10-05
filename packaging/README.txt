BFME2 Accelerator - experimental development build
===================================================

This fork adds verified BFME II 1.06 bindings and offline tests. There is no
measured whole-game FPS claim for this development build. Function benchmark
ratios are documented in docs/bfme2-optimization.md in the repository.

Running it
----------
Keep bfme2_accel_loader.exe and bfme2_accel.dll together in a folder.
Run the loader and select the game. Unavailable games can be configured through
Paths. Read bfme2_accel.log next to the DLL for recognition and installed hooks.
The packaged DLL is the production build, with diagnostic telemetry omitted.

Default BFME II vanilla 1.06 optimizations
----------------------------------------
Launch bfme2_accel_loader.exe normally. Audio indexing, render sorting, template
equivalence, native string comparisons packet processing and exact packet/RNG hashing and IEEE CRC32 are enabled by
default for recognized vanilla 1.06 code with matching byte guards.

For troubleshooting, each feature can still be disabled independently by setting
its environment variable to 0 before launching:

  set BFME2_AUDIOINDEX=0
  set BFME2_RLSORT=0
  set BFME2_EQUIVFAST=0
  set BFME2_STRINGFAST=0
  set BFME2_NETFAST=0
  set BFME2_CRCFAST=0
  set BFME2_CRC32FAST=0
  bfme2_accel_loader.exe

Unset variables enable the defaults. AOTR_AUDIOLIMIT=0 also disables indexing.
Unknown executables or changed guards retain stock behavior.

Audio indexing reduces repeated request-list traversal. Render sorting avoids
reference-count churn while preserving stock record ordering. Template
equivalence has mixed benchmark results: override chains improved, but the
no-override case was slightly slower. Its usefulness needs an actual battle
profile. Native string comparison removes wrapper calls and reads current
headers directly, with bounded SIMD comparison and original locale fallback.
Non-C locales keep the original CRT comparison for ASCII bytes too.
All four retain stock comparisons and disable on detected mismatches.

The accelerator loader injects changes into the running process; it does not
rewrite game.dat. The separate optional LAA tool below creates a patched copy.
Correctness tests do not establish battle performance, graphical/audio behavior,
save/replay compatibility, multiplayer determinism or long-session stability.
Those in-game checks remain outstanding. This is a testing build, not a release.

Implementation and evidence:
https://github.com/admiraly/bfme2-accelerator/tree/main

Optional 4 GB address-space patch (64-bit Windows)
------------------------------------------------
Requires Python 3. Close the game. From this folder:

  python tools/bfme2_laa.py "C:\Games\BFME II\game.dat" --output "C:\Games\BFME II\game.laa.dat"

Substitute your actual installation path. Only the pinned vanilla 1.06 image
and its LAA-only variant are accepted. Existing files are never overwritten.
Keep original game.dat as game.dat.pre-laa, then rename game.laa.dat to game.dat.
Restart the game. To undo, restore game.dat.pre-laa as game.dat.
This raises the x86 virtual address-space ceiling from 2 GB to 4 GB on 64-bit
Windows. High-address game stability is not yet tested. This is separate from
the accelerator's runtime-only hooks; generating a copy does not install it.

BFME2_NETFAST replaces only the byte-identical packet scrambling loops. It
retains packet format, CRC, socket operations, timing and command order. Native
byte equivalence is checked offline and sampled at runtime; two-client testing
remains outstanding. It does not reduce ping.
