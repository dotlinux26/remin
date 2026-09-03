# Workspace Model

The heart of Remin is a small, deliberately simple domain model:

```
Workspace
 └── Windows[]
      └── Tabs[]
           └── PaneTree
                ├── Pane (leaf: terminal state)
                └── Split (two children + ratio)
```

## Why this shape

Remin must restore the *exact* layout of a session. Splitting a terminal pane
yields an arbitrary binary tree of panes, so we model it as a real tree rather
than a flat list. Each tree node is either:

- a **`Pane`** (leaf) carrying the terminal state, or
- a **`Split`** with exactly two children and a `ratio` (0..1) describing where
  the divider sits.

`Workspace → Window → Tab → Pane` matches how GNOME users think: a window holds
tabs, a tab holds a pane tree.

## Type-safe ids

Every domain object has a distinct id type (`WorkspaceId`, `WindowId`, `TabId`,
`PaneId`, `SnapshotId`) built on a single template `Id<Tag>`.

```cpp
using WorkspaceId = Id<WorkspaceTag>;   // won't mix with TabId
using TabId      = Id<TabTag>;
```

Two properties matter for IPC:

- Ids are **strings**, stable across processes (a CLI string id must equal a GUI
  object id).
- Generation is **locale-independent** (see `Id::generate()`), so ids look the
  same no matter what `setlocale` a frontend (GTK) triggered.

## PaneTree

`PaneTree` holds `unique_ptr` children, so it has explicit deep-copy
copy/assignment operators and a defaulted move. It caches the pane count and
offers:

- `leaf(Pane)` / `split(kind, first, second, ratio)` constructors,
- `first()` / `second()` / `ratio()` accessors,
- `collect_panes()` for depth-first traversal,
- deep copies so a workspace can be copied safely.

## Mutations

`WorkspaceCore` is the only thing that touches the tree. Key operations:

- **`split_pane(tab, kind, ratio)`** — wraps a bare pane in a split, or splits
  the focused leaf, inserting a brand-new pane on one side.
- **`remove_pane(tab, pane)`** — removes a leaf; if a split is left with one
  child, the child is *promoted* (so we never keep one-sided splits).
- **`set_pane_ratio(tab, pane, ratio)`** — rewrites the `ratio` on the pane's
  direct parent split.

All mutations persist through the `Storage` contract and emit a typed event
(`PaneSplit`, `PaneRemoved`, `PaneResized`, …) to the autosave path.

## Serialization

The whole workspace round-trips through JSON (nlohmann), including:

- workspace name + snapshot id list
- windows: id, title, focused tab
- tabs: id, title, focused pane
- the pane tree, preserving split kinds and ratios
- per-pane terminal state: cwd, shell, cols/rows, filtered environment,
  command history, and a captured `scrollback` buffer

`core/serialization.{hpp,cpp}` defines `to_json`/`from_json` for the model. Note
the JSON form is the *interchange/export* format; SQLite is the canonical store
(see [`storage.md`](storage.md)).

## Autosave integration

`WorkspaceCore` sets a `StateDirty` event (and other structural events) that the
GUI's `Autosaver` uses to decide when to flush. Structural mutations write
through synchronously; only high-frequency data (pane scrollback) is throttled.
See [`autosave-lock.md`](autosave-lock.md).
