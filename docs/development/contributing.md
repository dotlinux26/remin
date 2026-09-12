# Contributing

## Read first

- [AGENTS.md](../AGENTS.md) — the single source of truth: vision, architecture,
  conventions, current sprint, and the Window Identity & Persistence contract.
- [Architecture overview](../architecture/overview.md)
- [UI principles](../design/ui-principles.md)

## Working agreements

- **Design-first.** Non-trivial features get a design document under
  `docs/design/` and are gated before implementation.
- **Layered boundary** — View → Controller → Core → Storage/Runtime.
  `MainWindow` composes; `SessionController` orchestrates; `WorkspaceCore`
  owns state + invariants. Keep GUI dependent on nothing inside core.
- **No daemon (V1)** — one instance, single-window lock, no background
  processes.
- **Structural honesty** — if a subsystem is structurally wrong, refactor it;
  do not patch symptoms. A control's look must come from one shared
  style/component.

## Style

- C++20, gtkmm 4.
- CSS: `.remin-*` classes only, colors from `@accent/@text/@bg/@surface/
  @border/@text-muted`, 16px icons, 14px tab icons, 4px spacing grid.
- No arbitrary pixel hacks, no magic offsets, no font-shrinking to fix
  overflow, no scroll-polling, no ellipsis as a substitute for navigation.
- Keep `core/` and `storage/` dependency-free — they never pull in GTK/VTE.

## Building & testing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

See [building.md](building.md) and [testing.md](testing.md).

## The VTE patch series

Changes that touch terminal persistence belong in the patch series under
`patches/vte-0.76.0/`, not in application code. Follow
[`docs/patches/vte/upstream-delta.md`](../patches/vte/upstream-delta.md) when
moving versions; regenerate `SHA256SUMS` and keep the docs' claims truthful.