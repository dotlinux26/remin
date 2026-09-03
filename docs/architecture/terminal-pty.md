# Terminal & PTY

Remin talks to real shells. This layer (`src/terminal/`) isolates *how*,
so the core never depends on a specific platform.

## PTYProvider (abstraction)

`pty/pty_provider.hpp` defines the seam the rest of Remin relies on:

```cpp
class PtyProvider {
public:
    virtual std::unique_ptr<PtySession>
        spawn(const std::string& shell,
              const std::string& cwd,
              int cols = 80, int rows = 24,
              const std::vector<std::string>& env = {}) = 0;
};
```

`PtySession` carries the PTY **master fd**, the **child pid**, and the slave
name (`/dev/pts/N`). Keeping this behind an interface means a remote or mocked
PTY can be swapped in later without touching the core.

## Linux implementation: `forkpty()`

`pty/pty_linux.*` implements `PtyProvider` on Linux:

- `forkpty()` creates the master/slave pair and forks the shell child.
- The child execs the requested shell (with the correct TERM, e.g.
  `xterm-256color`) inside the new PTY.
- `TERM` is set explicitly so full-color apps render correctly under VTE.

Because the core depends only on `PtyProvider`, an alternate backend (e.g. a
remote `ssh -tt`) can be provided later without changing the workspace engine.

## Event loop: `poll()`, not `epoll`

`pty/event_loop.*` provides a tiny `poll()`-based reactor:

```cpp
loop.add_fd(master_fd, POLLIN, on_pty_read);
loop.run();
```

**Why poll()?** Remin manages a handful of PTY fds — not 10k sockets. `poll()`
is simpler and plenty fast at this scale. The loop is behind a small class so we
can optimize to `epoll()` later *only if* profiling actually shows a need (the
headless/PTY-only path, e.g. a future `remin tunnel`, might warrant it).

In the GUI, VTE already drives its own main loop; this `EventLoop` exists for
headless and IPC-driven terminal use.

## Shell detection

`shell/shell.{hpp,cpp}` decides which shell a new pane spawns:

1. `$SHELL` if set,
2. else the user's passwd entry,
3. else `/bin/sh`.

## Where the two meet

- The GUI (`gui/terminal/terminal_pane.cpp`) uses VTE, which manages its own PTY
  and renders the terminal — it does **not** use `PtyProvider` directly.
- `PtyProvider` + `EventLoop` are the building blocks for headless/IPC terminal
  sessions, keeping that capability ready without coupling to the GUI.
