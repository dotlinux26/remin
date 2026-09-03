# IPC Protocol

Remin uses a small **JSON-over-line** protocol for IPC (Unix domain socket) and
for the CLI's internal request path. It is JSON-RPC-ish but intentionally
minimal.

## Transport

- **Socket**: Unix domain socket (default path from `IpcServer::SocketPath`).
- **Framing**: one JSON object per line (`\n`-delimited).
- **No freshness handshake in V1**: each request maps directly to a
  `WorkspaceCore` method.

## Request

```json
{ "method": "workspace.create", "params": { "name": "pentest-lab" } }
```

- `method` — string, required.
- `params` — object, optional (default `{}`).

## Response envelope

```json
{ "ok": true, "result": { "...": "..." } }
```

```json
{ "ok": false, "error": "unknown method: foo" }
```

- `ok` — success flag.
- `result` — present on success, shape varies per method.
- `error` — present on failure.

## Methods

| method                | params                                        | result on success                  |
|-----------------------|-----------------------------------------------|------------------------------------|
| `workspace.list`      | —                                             | `{ "open": bool, "name": string? }`|
| `workspace.create`    | `{ "name": string }`                          | `{ "workspace_id": string }`       |
| `workspace.open`      | `{ "workspace_id": string }`                  | `{ "opened": true }`               |
| `workspace.close`     | —                                             | —                                  |
| `workspace.rename`    | `{ "workspace_id": string, "name": string }`  | —                                  |
| `window.add`          | `{ "title": string }`                         | `{ "window_id": string }`          |
| `window.rename`       | `{ "window_id": string, "name": string }`     | —                                  |
| `snapshot.create`     | —                                             | `{ "snapshot_id": string }`        |

Ids are the string ids produced by `remin::core::Id<T>::generate()`.

## Example round-trip

```
> { "method": "workspace.create", "params": { "name": "audit" } }
< { "ok": true, "result": { "workspace_id": "3771e48809a0..." } }
```
