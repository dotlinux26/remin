# Contributing

1. Follow the ADRs in `docs/decisions/`.
2. Build with `cmake --preset dev` and keep warnings clean.
3. Add tests under `tests/`; run `ctest --preset dev`.
4. Keep the core independent of GUI/IPC.
