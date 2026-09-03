# Autosave & Locking

Two Phase-5 features; both live in `src/core/` (the autosave **policy** hooks
are configured by the GUI session).

## Edge-triggered autosave (not a timer)

`Autosaver` (`core/autosave.{hpp,cpp}`) implements Remin's save policy. The key
decision (ADR-0006): **no blind periodic autosave**.

Instead, saving is *triggered by activity* and *debounced*:

```
keystrokes:      |k||k||k|               |k|
activity set:    x  x   x                   x
debounce timer:  ....>flush     [idle: nothing]   ....>flush
```

- The host calls `note_activity(id)` on every change (GTK hooks VTE's `commit`
  signal for terminals, and the note buffer's `changed` signal for notes). This
  only inserts the id into a set — cheap.
- A `due()` check (driven by a lightweight 250 ms GTK idle poll) becomes true
  only after the debounce window has elapsed since the *last* activity.
- `flush()` — called once when due — clears the set and returns to idle.

### One system, per-resource policy

There is **one** `Autosaver` (owned by `WorkspaceSession`, never by a widget),
but the *save policy* differs by resource type:

| Resource | Trigger        | Policy        |
|----------|----------------|---------------|
| Terminal | typing         | debounce 2 s  |
| Note     | buffer change  | idle 10 s     |
| Explicit | menu/action    | immediate     |
| Shutdown | window/quit    | immediate     |

Widgets only emit the semantic signal ("document changed", "workspace changed");
the session decides **when** to persist. Notes and terminals share the same
pipeline, so there is no second, parallel autosave mechanism in the editor.

Result: a quiet terminal writes **nothing**; only editing writes, and only
**once per burst**. Unique to Remin vs. save-on-timer tools.

### Scrollback is captured lazily

`Autosaver` never reads the terminal on a keystroke. `flush()` asks a
provider for each pending resource's *current* payload and persists it, so a
burst's cost is exactly one read + one SQLite write.

### Testability

The clock is injectable (`set_clock`), so unit tests fast-forward time and assert
the "one flush per burst, nothing while idle" property exactly
(`tests/unit/autosave_test.cpp`).

## Workspace lock

`WorkspaceLock` (`core/workspace_lock.{hpp,cpp}`) is an advisory exclusive lock
backed by `flock(2)` on a per-workspace lock file:

```
<XDG_DATA_HOME>/remin/locks/<workspace-id>.lock
```

- The GUI process acquires it in `WorkspaceSession`; a second instance on the
  same workspace is refused while the first is alive.
- The OS releases the lock when the holding process exits, so **stale locks
  self-heal** — no manual cleanup.
- Verified by `tests/unit/workspace_lock_test.cpp` (lock, refuse, cross-workspace
  independence, release-and-reacquire).

## Together

`WorkspaceSession` (`gui/session/workspace_session.cpp`) owns both: it opens a
default workspace, takes the lock, and hands the controller + autosaver to the
GUI shell. The autosave badge in the GUI top-right shows `saved ✓` /
`save failed` after each flush.
