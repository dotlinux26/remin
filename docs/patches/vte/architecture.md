# Architecture

The extension adds three entry points to the VTE terminal and wires them
through the existing `vte.cc` implementation and the GObject wrappers.

## Public API

```c
gboolean vte_terminal_snapshot_capture (VteTerminal *terminal,
                                        GBytes     **snapshot_data);

gboolean vte_terminal_snapshot_restore (VteTerminal *terminal,
                                        GBytes      *snapshot_data);

gboolean vte_terminal_snapshot_has_pending_data (VteTerminal *terminal);
```

- `snapshot_capture()` → serializes the full internal emulator state into an
  **opaque binary snapshot** (`GBytes`).
- `snapshot_restore()` → rebuilds that state into the terminal.
- `snapshot_has_pending_data()` → a readiness probe (currently a stub that
  reports `TRUE`; see [Compatibility](compatibility.md)).

## Layout of the patch series

The series is split into four patches so each commit stays reviewable and the
public/debug separation is explicit:

| Patch | Scope |
|-------|-------|
| `0001` | public API declarations in `vte/vte.h.in` |
| `0002` | internal declarations (C++ `Terminal` methods) |
| `0003` | the serializer/restorer in `src/vte.cc` |
| `0004` | GObject-level wrappers exposed on `VteTerminal` |

## The snapshot is captured from the canonical state

Capture reads directly from VTE's own internal structures — screen rings,
modes, palette, tab stops, scrolling region, and terminal-wide defaults — it
does not scrape the widget or render anything. That is why the snapshot
represents *state*, not a picture of the screen.

Key design choices:

- **Both screens** are serialized (normal + alternate), plus which one is
  active.
- **Ring positions are stored relative to the ring delta**, so restore works
  regardless of where the ring happened to be when the app restarted.
- **Hyperlinks** are stored as URL strings in the ring and re-registered by
  index on restore, surviving frozen and writable rows uniformly.
- **Stream (line-buffering) data is NOT serialized** — VTE regenerates streams
  naturally as new rows freeze after restore.

See [snapshot-format.md](snapshot-format.md) for the logical structure and
[restoration-semantics.md](restoration-semantics.md) for the guarantees.