#include "gui/window/terminal_tab_view.hpp"
#include "gui/session/session_controller.hpp"
#include "terminal/shell/shell.hpp"

namespace remin::gui {

TerminalTabView::TerminalTabView(SessionController* controller,
                                 remin::core::WindowId window,
                                 remin::core::TabId tab,
                                 remin::core::PaneId root_pane)
    : controller_(controller),
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

void TerminalTabView::show_pane_menu(Gtk::Widget& pane_widget) {
    // Tear down any existing menu popover before showing a new one.
    if (pane_menu_) {
        pane_menu_->popdown();
        pane_menu_->unparent();
        pane_menu_ = nullptr;
    }

    auto* menu = Gtk::make_managed<Gtk::Popover>();
    menu->add_css_class("remin-context-menu");

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    box->set_margin(6);

    auto add_item = [menu, box](const char* label, std::function<void()> cb) {
        auto* b = Gtk::make_managed<Gtk::Button>(label);
        b->add_css_class("remin-menu-item");
        b->set_halign(Gtk::Align::FILL);
        b->signal_clicked().connect([menu, cb]() {
            menu->popdown();
            if (cb) cb();
        });
        box->append(*b);
        return b;
    };

    add_item("Split Horizontally", [this]() {
        split(remin::core::PaneTree::Kind::SplitHorizontal);
    });
    add_item("Split Vertically", [this]() {
        split(remin::core::PaneTree::Kind::SplitVertical);
    });
    add_item("Close Pane", [this]() { close_focused_pane(); });
    add_item("Color Profile…", [this]() {
        if (on_color_request_) on_color_request_();
    });

    menu->set_child(*box);
    menu->set_parent(pane_widget);
    menu->signal_closed().connect([this, menu]() {
        if (pane_menu_ == menu) pane_menu_ = nullptr;
        menu->unparent();
    });
    pane_menu_ = menu;
    menu->popup();
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

    // The split takes focus on the newly-created pane.
    active_pane_ = new_pane;
    rebuild();
    return new_pane;
}

bool TerminalTabView::close_focused_pane() {
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
            auto wid = pid;
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
                // Add active class to newly focused pane
                if (auto* curr = pane(wid)) {
                    curr->widget().add_css_class("remin-pane-active");
                }
            });
            auto right_click = Gtk::GestureClick::create();
            right_click->set_button(3); // Right-click
            right_click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
            right_click->signal_pressed().connect([this, wid](int, double, double) {
                if (auto* p = pane(wid)) {
                    show_pane_menu(p->widget());
                }
            });
            // Set initial active state for the first/root pane
            if (active_pane_.empty() && pid == root_pane_) {
                it->second->widget().add_css_class("remin-pane-active");
            }
            it->second->widget().add_controller(click);
            it->second->widget().add_controller(right_click);
            return it->second->widget();
        }
        case remin::core::PaneTree::Kind::SplitHorizontal:
        case remin::core::PaneTree::Kind::SplitVertical: {
            auto* paned = Gtk::make_managed<Gtk::Paned>();
            paned->set_orientation(node.kind() == remin::core::PaneTree::Kind::SplitHorizontal
                                       ? Gtk::Orientation::HORIZONTAL
                                       : Gtk::Orientation::VERTICAL);
            paned->set_hexpand(true);
            paned->set_vexpand(true);
            paned->set_start_child(build_node(*node.first()));
            paned->set_end_child(build_node(*node.second()));

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
    // Remove all children then rebuild from core state.
    while (tree_host_->get_first_child()) {
        tree_host_->remove(*tree_host_->get_first_child());
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
}

} // namespace remin::gui
