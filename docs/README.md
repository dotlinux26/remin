# Remin Documentation

Remin is a **Linux-native CLI workspace application**: terminals, notes, and a
persisting workspace in one window, carried across restarts.

## For users

- [Getting started](getting-started.md) — install, launch, first session
- [Installation](installation.md) — AppImage, Debian package, source build
- [Configuration](configuration.md) — settings, data directory, autosave
- [Usage](usage/workspaces.md) — the workspace model, windows / tabs / panes

## Usage guides

- [Workspaces](usage/workspaces.md)
- [Terminal](usage/terminal.md)
- [Command history](usage/history.md)
- [Notes](usage/notes.md)
- [Markdown](usage/markdown.md)
- [Export (HTML / PDF)](usage/export.md)

## For developers

- [Architecture](architecture/overview.md) — how the system is built
- [Design](design/) — why decisions were made (`docs/design/`)
- [Decisions](decisions/) — ADRs
- [Protocols](protocols/) — wire / IPC specs
- [Development](development/build.md) — building, testing, release checklist
- [Research notes](development/notes/) — historical reports & audits

## The VTE extension

Remin extends VTE with a minimal terminal snapshot/restore interface. This is
the most distinctive part of the project.

- [The Remin VTE extension](patches/vte/README.md) — the whole story
- Patch files: [`patches/vte-0.76.0/`](../patches/vte-0.76.0/)

## Release engineering

- [Runtime & Linking Policy](../runtime-linking-policy.md) — how official
  binaries are linked and packaged (the VTE must never be system-provided)
- [Packaging](packaging/appimage.md) — AppImage
- [Debian package](packaging/debian.md)
- [Desktop integration](packaging/desktop-integration.md)
- [Release checklist](development/release.md)