# Contributing

## Workflow
1. Fork + branch from main.
2. Read AGENTS.md (vision + architecture) first.
3. Follow design docs in docs/design/ (esp. workspace-persistence pipeline).
4. Build: `cmake --preset dev`; tests: `ctest --preset dev` (22 suites).
4. Commit small, clear messages; **do not break the P5 window-identity contract**.

## Code style
- C++20, namespace `remin::`, CSS `.remin-*`, actions `win.<name>`.
- No unnecessary comments; follow UI principles (docs/design/ui-principles.md).

## Testing
- New features need unit tests under `tests/unit/`.
- Run CI (`.github/workflows/ci.yml`) before opening a PR.

## Releasing
- See docs/development/release.md (reproducible pipeline + VTE gate).