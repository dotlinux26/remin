# ADR-0005: IPC via Unix domain socket

## Decision
Use a Unix domain socket for CLI↔GUI process communication.

## Reason
A `remin gui` process owns the WorkspaceCore; a `remin workspace ...` CLI
invocation must reach it. UDS is native, secure, no extra deps.

## Rejected
- No IPC (CLI can't drive a running GUI).
- D-Bus/other brokers — extra moving parts for V1.
