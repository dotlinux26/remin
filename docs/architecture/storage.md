# Storage

Remin persists its canonical state in **SQLite** (ADR-0004). JSON/nlohmann is
used only for interchange and export, not as the database.

## Deployments

- Single file: `~/.local/share/remin/remin.db` (XDG data dir, overridden by
  `XDG_DATA_HOME`), or `/tmp/remin` if no home is set.
- SQLite gives us: no daemon, ACID transactions, crash recovery, and
  multi-process locking for free.

## Schema

```sql
CREATE TABLE workspaces (
    id               TEXT PRIMARY KEY,
    name             TEXT NOT NULL,
    working_directory TEXT NOT NULL DEFAULT '',
    created_at       TEXT NOT NULL,
    last_saved_at    TEXT NOT NULL,
    json             TEXT NOT NULL           -- full workspace, JSON
);

CREATE TABLE snapshots (
    id           TEXT NOT NULL,
    workspace_id TEXT NOT NULL,
    timestamp    TEXT NOT NULL,
    revision     INTEGER NOT NULL,
    size_bytes   INTEGER NOT NULL DEFAULT 0,
    state_json   TEXT NOT NULL,
    PRIMARY KEY (workspace_id, id)
) WITHOUT ROWID;

CREATE TABLE scrollbacks (
    pane_id    TEXT PRIMARY KEY,
    content    TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

- `workspaces`: one row per workspace; the `json` column holds the last saved
  serialized `Workspace` (see [`workspace-model.md`](workspace-model.md)).
- `snapshots`: point-in-time copies of a workspace's state, keyed by workspace.
- `scrollbacks`: per-pane captured terminal buffer text, written on autosave.

Timestamps are ISO-8601 strings (handled by `core/serialization.hpp`).

## Storage interface

`Storage` (in `core/workspace_core.hpp`) is the contract the engine depends on.
`SqliteStorage` implements it:

```cpp
// workspaces
list_workspaces(); load_workspace(id);
save_workspace(ws); delete_workspace(id);
// snapshots
list_snapshots(ws); load_snapshot(ws, snap);
save_snapshot(ws, snap, state); delete_snapshot(ws, snap);
// scrollback
store_scrollback(pane, content); load_scrollback(pane);
```

Because the engine only sees the interface, tests can substitute an in-memory
`FakeStorage` (see `tests/unit/workspace_core_test.cpp`).

## SQLite connection

`SqliteDb` wraps the vendored amalgamation (`third_party/sqlite/sqlite3.c`),
built with `SQLITE_THREADSAFE=1` and `FOREIGN_KEYS=1`. It runs in **WAL** mode
for better concurrent read/write between the GUI and a short-lived CLI.

*Vendored:* the amalgamation ships under `third_party/sqlite/` so builds need no
system sqlite dependency.

## Snapshots

`create_snapshot()` serializes the current workspace to JSON, records a
`Snapshot` (timestamp, revision, size), persists the state row, and appends the
id to the workspace. `restore_snapshot()` reloads the stored JSON back into the
in-memory `Workspace`.

## Autosave + locking

The details live in [`autosave-lock.md`](autosave-lock.md); in short, structural
changes write through synchronously, while high-frequency pane scrollback is
persisted by the `Autosaver` at the end of each typing burst.
