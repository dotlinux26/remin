# IPC & CLI

Remin has two driving frontends besides the GUI: a **CLI** and an **IPC layer**
that lets a CLI reach a live session.

## CLI frontend

`src/app/main.cpp` is the single `remin` binary. Flow:

1. Parse `argv` into a command (`gui`, `workspace list`, `window add`, …).
2. `ensure_core()` builds `SqliteStorage` + `WorkspaceCore`.
3. `RequestDispatcher` (`cli/commands/request_dispatcher.{hpp,cpp}`) turns the
   subcommand into a JSON request:
   ```json
   { "method": "workspace.create", "params": { "name": "pentest-lab" } }
   ```
4. `WorkspaceCore` executes; the dispatcher returns a JSON response printed to
   stdout.

Because each CLI run is short-lived and `WorkspaceCore` writes through its
`Storage`, a CLI command can open the same database the GUI uses — SQLite
handles the concurrency.

## IPC layer (Unix domain socket)

`src/ipc/` provides JSON-over-socket communication:

- **`IpcServer`** (`server/ipc_server.cpp`) — a UDS server that would be hosted
  *inside* the GUI process. Each request is a line-delimited JSON message; the
  response is one JSON line. It exposes `listen_fd()` for easy integration with
  the app's event loop.
- **`IpcClient`** (`client/ipc_client.cpp`) — the other end, used by `remin`
  when it wants to talk to a *running* session instead of acting directly.

> Current state: the `IpcServer`/`IpcClient` primitives compile and the request
> format is defined. Wiring the server into the GUI's main loop (so live
> `remin workspace …` commands reach the running session) is the remaining
> integration step.

## Rationale

- **No daemon** (ADR-0006) — the GUI process is the authority; an embedded UDS
  server is the natural way to let an external CLI drive it.
- **UDS + JSON** is native, secure, and dependency-free (ADR-0005).
- **One binary** — `gui`, `cli`, and (later) `ipc` are all frontends over the
  *same* `WorkspaceCore`.

See [`../protocols/ipc.md`](../protocols/ipc.md) for the wire format.
