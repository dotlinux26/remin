#pragma once

#include "core/id.hpp"
#include "core/pane/pane.hpp"

#include <string>
#include <vector>
#include <optional>

namespace remin::core {

// A tab within a window: a title and a tree of panes.
struct Tab {
    TabId id;
    std::string title;
    PaneTree pane_tree;

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
