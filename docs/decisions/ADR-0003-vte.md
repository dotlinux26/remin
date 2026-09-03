# ADR-0003: VTE GTK4

## Decision
Use VTE (gtk4 backend) as the terminal emulator.

## Reason
Do not implement terminal emulation ourselves. VTE already provides PTY,
scrollback, selection, and hyperlinks.

## Rejected
- libvterm + custom renderer
- custom ANSI parser
- tmux backend
