#pragma once

#include "core/id.hpp"
#include "core/pane/pane.hpp"

#include <string>
#include <vector>
#include <optional>

namespace remin::core {

// Semantic type of a tab, first-class in the core domain. GUI TabView maps
// 1:1 from here (not the other way). New kinds (Log, Diff, …) extend this enum.
enum class TabKind { Terminal, Note };

// Full note-tab state so a note surface can be rebuilt from a checkpoint.
struct NoteTabState {
    std::string document_id;
    std::string path;           // on-disk path ("" = temp draft)
    std::string title;
    std::string content;
    bool modified{false};
    int cursor{0};              // buffer offset
    double scroll{0.0};         // relative scroll 0..1 (best-effort)
    bool preview_enabled{false};
    double split_ratio{0.5};
    bool sync_scroll{false};
};

// A tab within a window: metadata + a tree of panes (or note state).
struct Tab {
    TabId id;
    std::string title;
    TabKind kind{TabKind::Terminal};
    std::optional<NoteTabState> note_state;  // only meaningful for kind == Note

    PaneTree pane_tree;  // still populated for Terminal tabs

    Tab() = default;
    // Helper for tests / construction.
    static Tab create(std::string title, TabId id = TabId::generate()) {
        Tab t;
        t.id = std::move(id);
        t.title = std::move(title);
        return t;
    }
};

// A window: metadata + ordered tabs + focused pane within focused tab.
struct Window {
    WindowId id;
    std::string title;
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint32_t width{0};
    std::uint32_t height{0};

    std::vector<Tab> tabs;
    std::optional<TabId> focus_tab_id;
    std::optional<PaneId> focus_pane_id;

    Window() = default;
    static Window create(std::string title, WindowId id = WindowId::generate()) {
        Window w;
        w.id = std::move(id);
        w.title = std::move(title);
        return w;
    }
};

} // namespace remin::core
