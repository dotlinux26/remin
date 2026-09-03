# ADR-0004: SQLite storage

## Decision
Use SQLite as the canonical storage backend.

## Reason
Embedded, no daemon, single file, ACID transactions, crash recovery, and
multi-process locking handled by the engine rather than hand-rolled.

## Rejected
- Flat JSON files as the database (no transactions/locking).
- Custom binary serialization (attack surface, migration headache).
