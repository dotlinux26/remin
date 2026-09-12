# Terminal History Semantics - Screen State / Command History / Transcript

Date: 2026-09-06
Status: DESIGN (semantics finalized with the user; not yet implemented)
Source problem doc: `docs/problem-terminal-transcript-capture.md`

---

## 1. The original problem

User workflow:

```text
$ ls
a.txt
b.txt
c.txt

$ clear

user@host:~$
```

After exiting Remin and reopening:

- Question 1: does `clear` erase the Remin history?
- Question 2: do we save the full log even after `clear`, or only what is actually
  visible on the pane screen?
- Question 3: Remin is not bash, so there is no `bash_history`; commands cannot be
  reviewed generically, only via Up/Down (up/down arrow keys). Is there a way to
  handle this?

User's finalized answer:

> `clear` is not "deleting Remin history." It is only an operation that changes the
> terminal's display state.

---

## 2. Three strictly separated concepts (finalized semantics)

Remin does not call everything "history." There are 3 distinct concepts:

### 2.1 Screen State ("the pane's current screen")

```text
current terminal-visible context
```

- Affected by `clear`.
- The **most recent** display state restored after a restart.
- After `clear`, screen state = empty prompt `user@host:~$`.

### 2.2 Command History ("commands the user has entered")

```text
canonical per-pane sequence of committed commands
```

- `clear` does **NOT** erase this.
- Survives restart.
- The source for History UI / search / provenance.
- Up/Down (readline navigation) **remains shell-owned** - Remin does not emulate readline.

### 2.3 Terminal Transcript ("what the terminal once rendered")

```text
historical terminal output available to Remin
```

- Independent of command history.
- `clear` does not necessarily erase persisted historical transcript.
- **Not necessarily replayed on the current screen after restore.**

### 2.4 Summary table

| Concept | Changed by `clear` | Restored after restart | Serves |
|---------|--------------------|------------------------|--------|
| Screen State | Yes (clear -> empty prompt) | Yes - restore most recent screen | pane screen |
| Command History | No | Yes - shell Up/Down + History UI | shell nav, search, provenance |
| Terminal Transcript | Not necessarily | Type-dependent - stored separately | History viewer, output log |

**`clear` is a screen-state operation, NOT a history-deletion operation.**

---

## 3. Why not rely on `~/.bash_history` / HISTFILE

Remin is not a shell, but a real bash/zsh/fish runs underneath:

```text
Remin
  v
VTE
  v
PTY
  v
bash (zsh/fish)
```

Bash has its own history, but Remin **should not use `~/.bash_history` as the
canonical source**, because:

- the user may use zsh / fish
- `HISTFILE` may be disabled
- history may flush according to the shell's own policy
- multiple panes can share a shell type -> mixed-up history
- history scope does not reflect Remin pane identity

**Remin's canonical source = `Pane.command_history[]`.**

Shell history (HISTFILE) is only **runtime integration**, not a data source.

---

## 4. How to view "all commands" without bash_history

This is where Remin beats a normal terminal.

The **History** sidebar does NOT read `~/.bash_history`. It queries the Workspace
structure itself:

```text
Workspace
 '-- Windows
     '-- Tabs
         '-- Panes
             '-- command_history[]
```

**Example search:** `nmap`

```text
Window: GitLab Audit
Tab:    Recon
Pane:   2

nmap -sCV 10.10.10.10
nmap -p- 10.10.10.10
```

**Up/Down is left to the shell** - natural shell behavior, no readline emulation.
Remin simultaneously records committed commands for its own canonical history.

---

## 5. Two parallel flows (command recorder)

```text
User input
   v
PTY
   v
bash/zsh/fish
   v
VTE

                       '-- Remin command recorder
                           (records committed commands into command_history[])
```

What to AVOID: implementing Up/Down by having Remin pull from
`Pane.command_history[]` and feed it back into the terminal. The shell already has
readline/history navigation; Remin only observes and stores.

---

## 6. Proposed architecture - Screen / Journal

Instead of treating VTE scrollback as the single history source, we have:

```text
                    TerminalPane
                         |
              .---------------------.
              |                     |
        Screen/VTE               Journal
              |                     |
         current view        terminal events/log
```

**Example:**

```text
$ ls
a
b

$ clear

$ pwd
/home/user
```

| Layer | Content |
|-------|---------|
| Last screen snapshot | `$ pwd`  |  `/home/user` |
| Command history | `ls`  |  `clear`  |  `pwd` |
| Transcript | `ls`  |  `a`  |  `b`  |  `clear`  |  `pwd`  |  `/home/user` |

**After restart:**

- Pane displays: `$ pwd` / `/home/user`
- Up/Down: `pwd` -> `clear` -> `ls`
- History viewer: `10:32:01 ls`  |  `10:32:04 clear`  |  `10:32:07 pwd`

**This also solves `clear`:** if the full transcript is stored, `clear` is just an
**event** (`CLEAR`), not "DELETE EVERYTHING BEFORE HERE".

---

## 7. TerminalJournal (future - append-only)

```text
TerminalJournal
|-- INPUT
|-- OUTPUT
|-- RESIZE
|-- CLEAR
|-- SIGNAL
'-- ...
```

A full event journal is not needed for V1. V1 can be:

```text
current_screen
+
command_history
+
captured_scrollback
```

Upgrade to a journal later when retention needs exceed VTE scrollback.

---

## 8. Checkpoint behavior (finalized)

```text
current screen state  -> restore current screen
command history       -> restore per-pane history
transcript            -> retain separately when supported
```

- `clear` only changes **screen state**; command history + transcript survive.
- Restore shows the **screen exactly as the user last left it**, without rebuilding
  `$ ls ... $ clear` behind a screen that was already cleared.

---

## 9. Acceptance test (must pass)

Pane A:

```text
$ printf 'BEFORE_CLEAR\n'
BEFORE_CLEAR

$ clear

$ printf 'AFTER_CLEAR\n'
AFTER_CLEAR
```

**After restart:**

```text
Screen:   $ printf 'AFTER_CLEAR\n'
          AFTER_CLEAR

^v:       printf 'AFTER_CLEAR\n'
          clear
          printf 'BEFORE_CLEAR\n'

History UI:  BEFORE_CLEAR
             clear
             AFTER_CLEAR
```

---

## 10. Important misunderstanding to avoid

> VTE scrollback should not be considered the "entire working history" of a pane.

It is only the part the terminal emulator still holds/displays. Remin needs its own
state/history layer - that layer is what makes Remin different from a plain
terminal emulator.

---

## 11. Current code status (relevant)

- `PaneState` currently has: `scrollback`, `cwd`, `cols`, `rows`, `shell`,
  `command_history`, `interrupted_command` (`src/core/pane/pane.hpp`).
- `command_history` is written via `WorkspaceCore::add_command_to_pane` (cap 1000).
- `TerminalPane::capture_scrollback()` (`terminal_pane.cpp:188`) uses
  `vte_terminal_get_text_range_format` to get VTE scrollback - **this is the
  "captured screen" source, NOT the canonical transcript.**
- **CAPTURE FIDELITY is currently FAILING**: blob ~10KB but almost blank + prompt
  (see `docs/problem-terminal-transcript-capture.md`). Whether VTE returns more
  scrollback after `clear` is unconfirmed.

### Proposed model extension (to match the new semantics)

```text
PaneState
|-- terminal
|   |-- transcript            (text VTE still holds - currently captured scrollback)
|   |-- cols
|   |-- rows
|   '-- viewport marker       (V1: bottom/prompt if no exact offset)
|-- shell
|   |-- executable
|   |-- cwd
|   '-- command_history[]     (per-pane Up/Down + History UI)
'-- lifecycle
    '-- interrupted_command
```

---

## 12. Open technical questions (must be verified by test)

1. After `clear`, does VTE keep pre-clear lines in the scrollback buffer?
   - If YES -> captured scrollback still contains old output (correct transcript).
   - If NO -> Remin must keep its own journal/tab to retain transcript; VTE scrollback
     is only screen state.
2. Does the current capture (row range `-(rows + 10000)..rows`) return the whole
   buffer or only the visible region? (current evidence suggests it captures less
   than the whole.)
3. Is a separate `transcript` bucket needed, or reuse scrollback with new semantics?