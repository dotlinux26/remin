# Command History

Remin separates three concepts strictly — they are **not** the same thing:

```text
History
├── Commands      # what you actually ran (canonical, per-pane)
├── Transcripts   # full recorded terminal output (Remin-owned, clear-proof)
└── Windows       # (future) snapshots of closed workspaces
```

| Shortcut        | What it does                          |
|-----------------|---------------------------------------|
| `Ctrl+Shift+H`  | open the History panel                |
| `Ctrl+P`        | toggle the history sidebar            |

## Commands

Every pane keeps its **own canonical command list** (`command_history[]`,
capped at 1000 entries). Storing is done by Remin's core
(`WorkspaceCore::add_command_to_pane`), never by scraping `~/.bash_history`.

- Click/press-to-insert a command into the input line — it is **not executed**.
- History search runs Workspace → Windows → Tabs → Panes.
- Toggle tracking with `settings:command-history`.

`clear` does not touch this list.

## Transcripts

The full recorded terminal output for a pane lives in a Remin-owned transcript
path, independent of the current VTE screen. Because the transcript exists
outside the screen, `clear` cannot remove it; it accumulates as a pane-level
record.

## Windows (future)

Closed-window snapshots for recovering a whole workspace you dismissed. Closely
related to — but distinct from — the workspace recovery pipeline
([workspaces](workspaces.md)).

## Reference

The semantics are pinned in
[`docs/design/terminal-history-semantics.md`](../design/terminal-history-semantics.md)
and the 3-mode spec in
[`docs/design/history-system-spec.md`](../design/history-system-spec.md).