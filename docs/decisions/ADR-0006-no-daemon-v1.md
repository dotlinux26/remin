# ADR-0006: No daemon in V1

## Decision
Ship no background daemon in V1. The GUI process is the authority.

## Reason
Keeps the default deployment simple; autosave is handled by an in-process
checkpoint scheduler rather than a separate systemd service.

## Later
- Revisit a `remin background` headless mode only if really needed in V2+.
