# Restoration Semantics

The contract of the snapshot interface is a **state-level** one:

```text
    snapshot(A)
        ↓
    application restart
        ↓
    restore(snapshot(A))
        ↓
    continue using the same VTE terminal
```

The goal is not to reproduce the visible text. The goal is to restore the
terminal state itself.

## The semantic property

For supported future terminal actions `X`:

```text
RESTORE(snapshot(A)) + X  ≈  state(A) + X
```

In words: a terminal that was snapshot-captured, torn down, and restored
behaves as if it had simply continued running, for any supported action taken
afterwards.

## Example supported actions

Running a command, scrolling, full-screen apps (`vim`, `htop`, `less`, TUIs),
screen content edits — all continue correctly because the underlying state was
rebuilt.

## Retained state

After a restore, the terminal uniformly retains:

- screen contents (normal and alternate)
- scrollback contents
- cursor position and state
- alternate/normal screen mode and which is active
- terminal modes (ECMA + private bitmasks)
- default colors / attributes and palette
- scrolling region (DECSTBM/DECSLRM)
- tab stops
- hyperlinks
- character-replacement (charset) state
- other internal state required for behavioral continuity

## What is intentionally regenerated, not restored

- **Streams / line-buffering** — VTE rebuilds these as new rows freeze after
  restore; they are an optimization, not terminal state.
- **Visual scroll position** — a view concern.
- **child process / PTY** — the application respawns its shell after restore.

## Display-time considerations

Remin restores the snapshot and then feeds a single display-only
`\r\n` (visible output only, never sent to the child). This ensures the freshly
spawned shell's prompt lands on its own line instead of gluing onto the restored
prompt. See [Integration](integration.md) for the exact restore order.