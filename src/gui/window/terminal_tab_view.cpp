#include "gui/window/terminal_tab_view.hpp"
#include "gui/session/session_controller.hpp"
#include "terminal/shell/shell.hpp"

#include <algorithm>
#include <fstream>

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

    // Outer layout: [ resizable left sidebar | terminal pane tree ]
    root_paned_ = Gtk::make_managed<Gtk::Paned>();
    root_paned_->set_orientation(Gtk::Orientation::HORIZONTAL);
    root_paned_->set_start_child(*Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0));
    tree_host_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    tree_host_->set_hexpand(true);
    tree_host_->set_vexpand(true);
    root_paned_->set_end_child(*tree_host_);
    root_paned_->set_position(0);
    root_paned_->set_wide_handle(true);
    append(*root_paned_);

    build_sidebar();
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

void TerminalTabView::build_sidebar() {
    auto* sidebar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    sidebar->add_css_class("remin-sidebar");
    sidebar->set_size_request(180, -1);

    // Tab switcher: History | Directory
    auto* tab_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    tab_bar->add_css_class("remin-sidebar-tabs");
    
    auto* history_tab = Gtk::make_managed<Gtk::Button>("History");
    history_tab->add_css_class("remin-sidebar-tab");
    history_tab->set_hexpand(true);
    history_tab->signal_clicked().connect([this]() { set_sidebar_mode("history"); });
    
    auto* directory_tab = Gtk::make_managed<Gtk::Button>("Directory");
    directory_tab->add_css_class("remin-sidebar-tab");
    directory_tab->set_hexpand(true);
    directory_tab->signal_clicked().connect([this]() { set_sidebar_mode("directory"); });
    
    tab_bar->append(*history_tab);
    tab_bar->append(*directory_tab);
    sidebar->append(*tab_bar);

    // Stack for History and Directory views
    sidebar_stack_ = Gtk::make_managed<Gtk::Stack>();
    sidebar_stack_->set_vexpand(true);
    sidebar_stack_->set_transition_type(Gtk::StackTransitionType::SLIDE_LEFT_RIGHT);
    sidebar->append(*sidebar_stack_);

    // History page
    history_scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    history_scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    history_scroller_->set_vexpand(true);
    history_list_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    history_scroller_->set_child(*history_list_);
    sidebar_stack_->add(*history_scroller_, "history", "History");

    // Directory page
    directory_scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    directory_scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    directory_scroller_->set_vexpand(true);
    directory_list_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    directory_scroller_->set_child(*directory_list_);
    sidebar_stack_->add(*directory_scroller_, "directory", "Directory");

    // Set initial directory to current working directory
    current_dir_ = std::filesystem::current_path();
    refresh_directory();

    root_paned_->set_start_child(*sidebar);
    set_sidebar_mode("history");
}

void TerminalTabView::update_sidebar() {
    if (!history_list_) return;
    // Rebuild the list of history entries.
    while (auto* child = history_list_->get_first_child()) history_list_->remove(*child);

    auto const add = [this](const std::string& cmd, guint index) {
        auto* btn = Gtk::make_managed<Gtk::Button>(cmd);
        btn->add_css_class("remin-history-item");
        btn->set_halign(Gtk::Align::FILL);
        btn->signal_clicked().connect([this, index]() {
            std::string c = history_[index];
            if (auto* p = focused_pane()) p->feed(c);
        });
        history_list_->append(*btn);
    };

    const auto back = std::min<std::size_t>(history_.size(), 500);
    const auto start = history_.size() - back;
    for (std::size_t i = 0; i < back; ++i) {
        add(history_[start + i], static_cast<guint>(start + i));
    }
    if (history_scroller_) {
        auto v = history_scroller_->get_vadjustment();
        if (v) v->set_value(v->get_upper());
    }
}

void TerminalTabView::add_history(const std::string& command) {
    if (command.empty()) return;
    history_.push_back(command);
    // Avoid unbounded growth.
    if (history_.size() > 2000) history_.erase(history_.begin(), history_.begin() + (history_.size() - 1000));
    update_sidebar();
}

void TerminalTabView::toggle_sidebar() {
    if (!root_paned_) return;
    if (root_paned_->get_position() <= 1) {
        root_paned_->set_position(200);
    } else {
        root_paned_->set_position(0);
    }
}

void TerminalTabView::clear_sidebar() {
    history_.clear();
    if (history_list_) {
        while (auto* child = history_list_->get_first_child()) history_list_->remove(*child);
    }
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
            auto* w = &it->second->widget();
            auto click = Gtk::GestureClick::create();
            click->set_button(0); // any button
            // Capture phase so we see right-clicks before VTE's own handling.
            click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
            click->signal_pressed().connect([this, wid, w](int n, double, double) {
                // Remove active class from previously active pane
                if (!active_pane_.empty()) {
                    if (auto* prev = pane(active_pane_)) {
                        prev->widget().remove_css_class("remin-pane-active");
                    }
                }
                active_pane_ = wid;
                // Add active class to newly focused pane
                w->add_css_class("remin-pane-active");
                if (n == 3) show_pane_menu(*w);
            });
            // Set initial active state for the first/root pane
            if (active_pane_.empty() && pid == root_pane_) {
                w->add_css_class("remin-pane-active");
            }
            w->add_controller(click);
            return *w;
        }
        case remin::core::PaneTree::Kind::SplitHorizontal:
        case remin::core::PaneTree::Kind::SplitVertical: {
            auto* paned = Gtk::make_managed<Gtk::Paned>();
            paned->set_orientation(node.kind() == remin::core::PaneTree::Kind::SplitHorizontal
                                       ? Gtk::Orientation::HORIZONTAL
                                       : Gtk::Orientation::VERTICAL);
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
    return *Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
}

void TerminalTabView::rebuild() {
    if (!tree_host_) return;
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

void TerminalTabView::set_sidebar_mode(const std::string& mode) {
    if (!sidebar_stack_) return;
    if (mode == "history") {
        sidebar_stack_->set_visible_child(*history_scroller_);
    } else if (mode == "directory") {
        sidebar_stack_->set_visible_child(*directory_scroller_);
        refresh_directory();
    }
}

void TerminalTabView::refresh_directory() {
    if (!directory_list_) return;
    // Clear current directory list
    while (auto* child = directory_list_->get_first_child()) directory_list_->remove(*child);

    // Add parent directory entry (..) if not at root
    if (current_dir_.has_parent_path()) {
        auto* parent_btn = Gtk::make_managed<Gtk::Button>("..");
        parent_btn->add_css_class("remin-directory-item");
        parent_btn->set_halign(Gtk::Align::FILL);
        parent_btn->signal_clicked().connect([this]() {
            current_dir_ = current_dir_.parent_path();
            refresh_directory();
        });
        directory_list_->append(*parent_btn);
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(current_dir_)) {
            std::string name = entry.path().filename().string();
            auto* btn = Gtk::make_managed<Gtk::Button>(name);
            btn->add_css_class("remin-directory-item");
            btn->set_halign(Gtk::Align::FILL);
            
            if (entry.is_directory()) {
                btn->signal_clicked().connect([this, name]() {
                    current_dir_ /= name;
                    refresh_directory();
                });
            } else if (entry.is_regular_file()) {
                // Check if file is readable (not binary) by trying to read first few bytes
                btn->signal_clicked().connect([this, name]() {
                    std::filesystem::path filepath = current_dir_ / name;
                    if (is_text_file(filepath)) {
                        open_file_in_editor(filepath);
                    }
                });
            }
            directory_list_->append(*btn);
        }
    } catch (const std::exception&) {
        // Ignore filesystem errors
    }
}

bool TerminalTabView::is_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize size = file.gcount();
    // Check for null bytes (common in binary files)
    for (std::streamsize i = 0; i < size; ++i) {
        if (buffer[i] == '\0') return false;
    }
    // Check if mostly printable ASCII
    int printable = 0;
    for (std::streamsize i = 0; i < size; ++i) {
        unsigned char c = buffer[i];
        if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t') {
            printable++;
        }
    }
    return size == 0 || (printable * 100 / size) > 70;
}

void TerminalTabView::open_file_in_editor(const std::filesystem::path& path) {
    if (on_open_file_) {
        on_open_file_(path);
    }
}

} // namespace remin::gui
