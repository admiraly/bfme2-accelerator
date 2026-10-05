# Repository workflow

- Target BFME II vanilla 1.06.
- The owner requested direct commits and pushes to `main`; do not create pull requests unless explicitly requested.
- Enable the verified BFME II audio, equivalence, sorting, string, packet and exact packet/RNG-hash optimizations by default. Retain individual environment-variable opt-outs (`0`), exact build/byte guards and stock mismatch fallbacks.
- Keep unverified engine bindings disabled. Default activation does not establish in-game, save/replay, multiplayer or high-address stability.
- Use the public GitHub Actions Linux and Windows x86 checks. Verify changes against the pinned native binary where applicable; describe microbenchmarks as CPU results, not FPS or ping improvements.
