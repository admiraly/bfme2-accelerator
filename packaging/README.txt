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

Experimental BFME II vanilla 1.06 options
----------------------------------------
From a Command Prompt in this folder, enable features independently:

  set BFME2_AUDIOINDEX=1
  set BFME2_RLSORT=1
  set BFME2_EQUIVFAST=1
  bfme2_accel_loader.exe

These BFME II engine features are OFF unless explicitly opted in. They require
recognized game code and matching byte guards. Leave a variable unset or set
it to 0 to keep that feature off. Test one feature at a time before combining.
AOTR_AUDIOLIMIT=0 also disables audio indexing.

Audio indexing reduces repeated request-list traversal. Render sorting avoids
reference-count churn while preserving stock record ordering. Template
equivalence has mixed benchmark results: override chains improved, but the
no-override case was slightly slower. Its usefulness needs an actual battle
profile. All three retain stock comparisons and disable on detected mismatches.

The loader injects changes into the running process; it does not rewrite game.dat.
Correctness tests do not establish battle performance, graphical/audio behavior,
save/replay compatibility, multiplayer determinism or long-session stability.
Those in-game checks remain outstanding. This is a testing build, not a release.

Implementation and evidence:
https://github.com/admiraly/bfme2-accelerator/pull/1
