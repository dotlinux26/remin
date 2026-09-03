# Remin — Architecture

This directory describes **how Remin is built**, for contributors and anyone who
wants to understand the codebase. Each file is one self-contained topic.

| Topic | File | What it covers |
|-------|------|----------------|
| Architecture overview | [`overview.md`](overview.md) | Big picture, component map, data flow |
| Workspace model | [`workspace-model.md`](workspace-model.md) | Core domain: `Workspace → Window → Tab → Pane` |
| Storage | [`storage.md`](storage.md) | SQLite backend, schema, snapshots, scrollback |
| Autosave & locking | [`autosave-lock.md`](autosave-lock.md) | Edge-triggered autosave, one system + per-resource policy, workspace lock |
| Notes & Markdown | [`notes.md`](notes.md) | Note editor, line numbers, md4c CommonMark → native GTK renderer |
| Terminal & PTY | [`terminal-pty.md`](terminal-pty.md) | PTY abstraction, `forkpty()`, event loop |
| GUI frontend | [`gui.md`](gui.md) | GTK4/VTE shell, session, logo, themes |
| IPC & CLI | [`ipc-cli.md`](ipc-cli.md) | Unix socket + CLI request dispatcher |

Cross-cutting decisions are recorded as ADRs in [`../decisions/`](../decisions/).
