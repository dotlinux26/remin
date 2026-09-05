#include "gui/window/terminal_tab_view.hpp"
#include "gui/window/main_window.hpp"
#include "gui/session/session_controller.hpp"
#include "gui/ui/context_menu.hpp"
#include "terminal/shell/shell.hpp"

#include <algorithm>
#include <cstdlib>

namespace {

// Object-data key storing a Paned's target divider ratio so we can restore it
// once the tree is mapped (after real allocation).
const char* const kPanedRatioKey = "remin-paned-ratio";

void set_paned_ratio(Gtk::Paned* paned, double ratio) {
    auto* boxed = static_cast<double*>(std::malloc(sizeof(double)));
    *boxed = std::clamp(ratio, 0.0, 1.0);
    g_object_set_data_full(G_OBJECT(paned->gobj()), kPanedRatioKey,
                           boxed, std::free);
}

void restore_paned_ratio(Gtk::Paned* paned) {
    auto* boxed = static_cast<double*>(
        g_object_get_data(G_OBJECT(paned->gobj()), kPanedRatioKey));
    if (!boxed) return;
    int total = paned->get_orientation() == Gtk::Orientation::HORIZONTAL
                    ? paned->get_width() : paned->get_height();
    if (total <= 0) return;
    paned->set_position(static_cast<int>(*boxed * total));
}

// Depth-first walk of a widget's descendants, restoring every paned ratio.
void walk_restore(Gtk::Widget& widget) {
    if (auto* paned = dynamic_cast<Gtk::Paned*>(&widget)) {
        restore_paned_ratio(paned);
    }
    auto* child = widget.get_first_child();
    while (child) {
        walk_restore(*child);
        child = child->get_next_sibling();
    }
}

} // anonymous namespace

namespace remin::gui {

TerminalTabView::TerminalTabView(SessionController* controller,
                                 MainWindow* main_window,
                                 remin::core::WindowId window,
                                 remin::core::TabId tab,
                                 remin::core::PaneId root_pane)
    : controller_(controller),
      main_window_(main_window),
      window_(std::move(window)),
      tab_(std::move(tab)),
      root_pane_(std::move(root_pane)),
      title_("terminal") {
    shell_ = remin::terminal::detect_default_shell();
    set_hexpand(true);
    set_vexpand(true);

    // Terminal pane tree fills the entire tab (sidebar is now at MainWindow level)
    tree_host_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    tree_host_->set_hexpand(true);
    tree_host_->set_vexpand(true);
    append(*tree_host_);

    // Wire command history from this tab to the controller
    rebuild();
    // Apply saved terminal colors to all panes
    load_and_apply_saved_colors();
}

TerminalTabView::~TerminalTabView() = default;

void TerminalTabView::activate() {
    if (active_pane_.empty()) return;
    auto it = panes_.find(active_pane_.str());
    if (it != panes_.end()) it->second->widget().grab_focus();
}

void TerminalTabView::deactivate() {}

bool TerminalTabView::focus_search() {
    // Ctrl+F on a terminal is handled by the window-level find bar; this tab
    // just reports that it can receive a find (returns true so it stays open).
    return true;
}

void TerminalTabView::clear_search() {
    for (auto& [id, pane] : panes_) {
        pane->clear_search();
    }
}

TerminalPane* TerminalTabView::pane(const remin::core::PaneId& id) {
    auto it = panes_.find(id.str());
    return it == panes_.end() ? nullptr : it->second.get();
}

TerminalPane* TerminalTabView::focused_pane() {
    auto id = active_pane_.empty() ? root_pane_ : active_pane_;
    return pane(id);
}

void TerminalTabView::add_history(const std::string& command) {
    if (command.empty()) return;
    // Persist to storage
    if (controller_) controller_->add_command_history(command);
    // Notify MainWindow's global sidebar
    if (on_history_) on_history_(command);
}

// Load and apply saved terminal colors to all panes in this tab
void TerminalTabView::load_and_apply_saved_colors() {
    if (!controller_) return;
    auto colors = controller_->terminal_colors();
    if (!colors) return;
    
    Gdk::RGBA fg, bg;
    gdk_rgba_parse(fg.gobj(), colors->foreground.c_str());
    gdk_rgba_parse(bg.gobj(), colors->background.c_str());
    
    for (auto& [id, pane] : panes_) {
        pane->set_colors(fg, bg);
    }
}

void TerminalTabView::show_pane_menu(Gtk::Widget& pane_widget, double x, double y) {
    // Tear down any existing menu popover before showing a new one.
    if (pane_menu_) {
        pane_menu_->popdown();
        pane_menu_->unparent();
        pane_menu_ = nullptr;
    }

    auto pid = active_pane_;
    auto* self = this;

    bool has_selection = false;
    if (auto* p = pane(pid)) has_selection = p->has_selection();

    // Build items for the shared ContextMenu.
    std::vector<ContextMenu::Item> items;
    items.push_back({"Copy (Ctrl+Shift+C)", [self, pid]() { if (auto* p = self->pane(pid)) p->copy_clipboard(); }, has_selection});
    items.push_back({"Paste (Ctrl+Shift+V)", [self, pid]() { if (auto* p = self->pane(pid)) p->paste_clipboard(); }});
    items.push_back({"Select All", [self, pid]() { if (auto* p = self->pane(pid)) p->select_all(); }});
    items.push_back({ContextMenu::SEPARATOR_LABEL, {}});
    items.push_back({"Split Horizontally (Alt+H)", [self]() { self->split(remin::core::PaneTree::Kind::SplitHorizontal); }});
    items.push_back({"Split Vertically (Alt+V)", [self]() { self->split(remin::core::PaneTree::Kind::SplitVertical); }});
    items.push_back({ContextMenu::SEPARATOR_LABEL, {}});
    items.push_back({"Clear Scrollback", [self, pid]() { if (auto* p = self->pane(pid)) p->clear_scrollback(); }});
    items.push_back({"Close Pane (Alt+K)", [self]() { self->close_focused_pane(); }});
    items.push_back({"Color Profile…", [self]() { if (self->on_color_request_) self->on_color_request_(); }});

    // Use the shared ContextMenu module — same look/behavior everywhere.
    ContextMenu::show(pane_widget, x, y, items);

    // Note: ContextMenu manages its own popover lifecycle. We can't easily track
    // the popover pointer for cleanup like before, but the shared module handles
    // cleanup on close. The pane_menu_ member is no longer needed for this menu.
}

std::optional<std::string> TerminalTabView::capture(const remin::core::PaneId& pane) const {
    auto it = panes_.find(pane.str());
    if (it == panes_.end()) return std::nullopt;
    return it->second.get()->capture_scrollback();
}

void TerminalTabView::activate_pane(const remin::core::PaneId& pane) {
    active_pane_ = pane;
}

remin::core::PaneId TerminalTabView::split(remin::core::PaneTree::Kind kind) {
    // Force a new pane in core; the new pane's id is returned.
    auto new_pane = controller_->split_pane(tab_, kind, 0.5);
    if (new_pane.empty()) return {};

    auto shell = shell_;
    auto pane = std::make_unique<TerminalPane>(shell, "");
    auto* raw = pane.get();
    if (controller_->autosaver()) {
        auto pid = new_pane;
        raw->set_input_callback([this, pid]() {
            controller_->autosaver()->note_terminal_activity(pid);
        });
    }
    raw->set_command_callback([this](std::string cmd) { add_history(std::move(cmd)); });
    panes_.emplace(new_pane.str(), std::move(pane));

    // Apply saved terminal colors to the new pane immediately
    if (controller_) {
        auto colors = controller_->terminal_colors();
        if (colors) {
            Gdk::RGBA fg, bg;
            gdk_rgba_parse(fg.gobj(), colors->foreground.c_str());
            gdk_rgba_parse(bg.gobj(), colors->background.c_str());
            raw->set_colors(fg, bg);
        }
    }

    // The split takes focus on the newly-created pane.
    active_pane_ = new_pane;
    rebuild();
    return new_pane;
}

bool TerminalTabView::close_focused_pane() {
    // If there's only one pane (no splits), close the entire tab instead.
    auto* core = controller_->core();
    if (core && core->current_workspace()) {
        for (const auto& win : core->current_workspace()->windows) {
            if (win.id == window_) {
                for (const auto& t : win.tabs) {
                    if (t.id == tab_) {
                        // Check if pane tree is a single leaf (no splits)
                        if (t.pane_tree.kind() == remin::core::PaneTree::Kind::Pane) {
                            // Single pane only -> close the whole tab
                            if (on_close_tab_request_) on_close_tab_request_();
                            return true;
                        }
                    }
                }
            }
        }
    }

    auto target = active_pane_.empty() ? root_pane_ : active_pane_;
    if (!controller_->remove_pane(tab_, target)) return false;
    panes_.erase(target.str());
    active_pane_ = root_pane_;
    if (!panes_.count(root_pane_.str())) {
        // Root was removed — promote an arbitrary remaining pane if any.
        if (!panes_.empty()) active_pane_ = remin::core::PaneId(panes_.begin()->first);
        else return false;
    }
    rebuild();
    return true;
}

void TerminalTabView::sync_ratio(Gtk::Paned& paned, const std::string& first_child_pane) {
    int total = paned.get_orientation() == Gtk::Orientation::HORIZONTAL
                    ? paned.get_width() : paned.get_height();
    if (total <= 0) return;
    double ratio = static_cast<double>(paned.get_position()) / static_cast<double>(total);
    ratio = std::clamp(ratio, 0.0, 1.0);
    if (!first_child_pane.empty()) {
        controller_->set_pane_ratio(tab_, remin::core::PaneId(first_child_pane), ratio);
    }
}

Gtk::Widget& TerminalTabView::build_node(const remin::core::PaneTree& node) {
    switch (node.kind()) {
        case remin::core::PaneTree::Kind::Pane: {
            if (!node.pane()) break;
            const auto& pid = node.pane()->id;
            auto it = panes_.find(pid.str());
            if (it == panes_.end()) {
                // Lazy-create a terminal for a pane we don't have yet.
                auto p = std::make_unique<TerminalPane>(shell_, "");
                auto* raw = p.get();
                if (controller_->autosaver()) {
                    auto cid = pid;
                    raw->set_input_callback([this, cid]() {
                        controller_->autosaver()->note_terminal_activity(cid);
                    });
                }
                raw->set_command_callback([this](std::string cmd) { add_history(std::move(cmd)); });
                panes_.emplace(pid.str(), std::move(p));
                it = panes_.find(pid.str());
            }
            // Track focused pane on click (GTK4: use GestureClick).
            // Only add controllers once per pane to avoid accumulation on rebuild.
            auto wid = pid;
            auto pane_key = wid.str();
            if (pane_controllers_added_.find(pane_key) == pane_controllers_added_.end()) {
                auto click = Gtk::GestureClick::create();
                click->set_button(0); // any button
                // Capture phase so we see right-clicks before VTE's own handling.
                click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
                click->signal_pressed().connect([this, wid](int, double, double) {
                    // Remove active class from previously active pane
                    if (!active_pane_.empty()) {
                        if (auto* prev = pane(active_pane_)) {
                            prev->widget().remove_css_class("remin-pane-active");
                        }
                    }
                    active_pane_ = wid;
                    // Record focus in core so split targets THIS pane.
                    controller_->focus_pane(tab_, wid);
                    // Add active class to newly focused pane
                    if (auto* curr = pane(wid)) {
                        curr->widget().add_css_class("remin-pane-active");
                    }
                });
                auto right_click = Gtk::GestureClick::create();
                right_click->set_button(3); // Right-click
                right_click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
                right_click->signal_pressed().connect([this, wid](int, double x, double y) {
                    if (auto* p = pane(wid)) {
                        show_pane_menu(p->widget(), x, y);
                    }
                });
                it->second->widget().add_controller(click);
                it->second->widget().add_controller(right_click);
                pane_controllers_added_.insert(pane_key);
            }
            // Set initial active state for the first/root pane
            if (active_pane_.empty() && pid == root_pane_) {
                it->second->widget().add_css_class("remin-pane-active");
            }
            auto& w = it->second->widget();
            if (auto* parent = w.get_parent()) {
                if (auto* box = dynamic_cast<Gtk::Box*>(parent)) box->remove(w);
                else if (auto* paned = dynamic_cast<Gtk::Paned*>(parent)) {
                    if (paned->get_start_child() == &w) paned->set_start_child(*Gtk::make_managed<Gtk::Box>());
                    else if (paned->get_end_child() == &w) paned->set_end_child(*Gtk::make_managed<Gtk::Box>());
                }
            }
            return w;
        }
        case remin::core::PaneTree::Kind::SplitHorizontal:
        case remin::core::PaneTree::Kind::SplitVertical: {
            auto* paned = Gtk::make_managed<Gtk::Paned>();
            paned->set_orientation(node.kind() == remin::core::PaneTree::Kind::SplitHorizontal
                                       ? Gtk::Orientation::VERTICAL
                                       : Gtk::Orientation::HORIZONTAL);
            paned->set_hexpand(true);
            paned->set_vexpand(true);
            auto& start_child = build_node(*node.first());
            auto& end_child = build_node(*node.second());
            // Ensure children are unparented before adding to new paned
            auto unparent = [](Gtk::Widget& child) {
                if (auto* parent = child.get_parent()) {
                    if (auto* box = dynamic_cast<Gtk::Box*>(parent)) box->remove(child);
                    else if (auto* paned = dynamic_cast<Gtk::Paned*>(parent)) {
                        if (paned->get_start_child() == &child) paned->set_start_child(*Gtk::make_managed<Gtk::Box>());
                        else if (paned->get_end_child() == &child) paned->set_end_child(*Gtk::make_managed<Gtk::Box>());
                    }
                }
            };
            unparent(start_child);
            unparent(end_child);
            paned->set_start_child(start_child);
            paned->set_end_child(end_child);

            // Restore the stored divider ratio so existing sibling panes keep
            // their size after a split/rebuild instead of being resized. The
            // actual position is applied once the tree is mapped.
            set_paned_ratio(paned, node.ratio());

            // Persist the divider ratio back to core. Capture a stable copy of
            // the first child's pane id (never a reference into core state).
            std::string first_child_pane;
            const auto* leaf = node.first();
            while (leaf && leaf->kind() != remin::core::PaneTree::Kind::Pane) {
                leaf = leaf->first();
            }
            if (leaf && leaf->pane()) first_child_pane = leaf->pane()->id.str();

            paned->property_position().signal_changed().connect(
                [this, paned, first_child_pane]() {
                    sync_ratio(*paned, first_child_pane);
                });
            return *paned;
        }
    }
    // Fallback: empty box (should not happen).
    auto* fallback = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    fallback->set_hexpand(true);
    fallback->set_vexpand(true);
    return *fallback;
}

void TerminalTabView::rebuild() {
    if (!tree_host_) return;
    // Clear active_pane_ BEFORE destroying widgets to avoid dangling widget pointers.
    active_pane_ = {};
    // Simply remove all children - GTK handles unparenting
    while (auto* child = tree_host_->get_first_child()) {
        tree_host_->remove(*child);
    }
    auto* core = controller_->core();
    if (!core || !core->current_workspace()) return;

    const remin::core::PaneTree* tree = nullptr;
    for (const auto& win : core->current_workspace()->windows) {
        if (win.id == window_) {
            for (const auto& t : win.tabs) {
                if (t.id == tab_) tree = &t.pane_tree;
            }
        }
    }
    if (!tree) return;
    tree_host_->append(build_node(*tree));
    // Once the newly-built tree has been sized by the layout engine, restore
    // each paned's divider to its stored ratio so sibling panes keep their
    // sizes after a split/rebuild instead of being reset.
    Glib::signal_idle().connect_once([this]() { walk_restore(*tree_host_); });
}

} // namespace remin::gui
