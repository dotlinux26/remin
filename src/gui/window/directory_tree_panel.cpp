#include "gui/window/directory_tree_panel.hpp"
#include "gui/ui/context_menu.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace remin::gui {

namespace {

// Object-data keys stored on each row container so the panel can find rows by
// their filesystem path (path-identity model) instead of chasing closure state.
constexpr char kPathKey[] = "remin-path";
constexpr char kBoxKey[] = "remin-box";
constexpr char kChildrenKey[] = "remin-children";
constexpr char kSizeKey[] = "remin-size";
constexpr char kArrowKey[] = "remin-arrow";

std::string get_path(Gtk::Widget& w) {
    const char* s = static_cast<const char*>(g_object_get_data(G_OBJECT(w.gobj()), kPathKey));
    return s ? s : std::string();
}

void set_path(Gtk::Widget& w, const std::filesystem::path& p) {
    g_object_set_data_full(G_OBJECT(w.gobj()), kPathKey,
                           g_strdup(p.string().c_str()),
                           static_cast<GDestroyNotify>(g_free));
}

Gtk::Widget* get_slot(Gtk::Widget& w, const char* key) {
    return static_cast<Gtk::Widget*>(g_object_get_data(G_OBJECT(w.gobj()), key));
}

void set_slot(Gtk::Widget& w, const char* key, Gtk::Widget* value) {
    g_object_set_data(G_OBJECT(w.gobj()), key, value);
}

std::string format_file_size(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024 * 1024 * 1024) {
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f MB", mb);
        return buf;
    }
    double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f GB", gb);
    return buf;
}

std::string format_file_time(const std::filesystem::file_time_type& ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&tt));
    return buf;
}

} // namespace

DirectoryTreePanel::DirectoryTreePanel(OpenFileCallback open_file)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 4), open_file_(std::move(open_file)) {

    state_.current_dir = get_home_dir();

    // Header: search box + reload button (top), path label (bottom)
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);

    // Top row: search entry + reload button
    auto* header_top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);

    search_ = Gtk::make_managed<Gtk::SearchEntry>();
    search_->add_css_class("remin-dir-search");
    search_->set_placeholder_text("Search files...");
    search_->set_hexpand(true);
    search_->signal_changed().connect([this]() {
        state_.filter = search_->get_text();
        refresh();
    });

    // Reload button: transparent, icon-only, small
    auto* reload = Gtk::make_managed<Gtk::Button>();
    reload->add_css_class("remin-dir-reload");
    reload->set_has_frame(false);
    reload->set_icon_name("view-refresh-symbolic");
    reload->set_tooltip_text("Reload files");
    reload->signal_clicked().connect([this]() { refresh_to_top(); });

    // Home button: transparent, icon-only, small (same style as reload)
    auto* home = Gtk::make_managed<Gtk::Button>();
    home->add_css_class("remin-dir-reload");
    home->set_has_frame(false);
    home->set_icon_name("user-home-symbolic");
    home->set_tooltip_text("Go to home directory");
    home->signal_clicked().connect([this]() {
        set_root(get_home_dir());
    });

    header_top->append(*search_);
    header_top->append(*reload);
    header_top->append(*home);

    header->append(*header_top);

    // Second row: path label showing current directory
    path_label_ = Gtk::make_managed<Gtk::Label>();
    path_label_->add_css_class("remin-dir-path");
    path_label_->set_halign(Gtk::Align::START);
    path_label_->set_ellipsize(Pango::EllipsizeMode::START);
    header->append(*path_label_);

    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_->set_vexpand(true);
    tree_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    scroller_->set_child(*tree_);

    append(*header);
    append(*scroller_);

    // The very first time the panel is shown, reset scroll to the top of the
    // listing. Initialization happens before the widget is realized/layout, so
    // a scroll value set then can be a no-op; the first map is the reliable
    // moment to force it.
    signal_map().connect([this]() {
        if (first_show_pending_) {
            first_show_pending_ = false;
            refresh_to_top();
        }
    });
}

void DirectoryTreePanel::start_monitor(const std::filesystem::path& dir) {
    monitor_.reset();
    try {
        auto file = Gio::File::create_for_path(dir.string());
        monitor_ = file->monitor_directory(Gio::FileMonitorFlags::NONE);
        // Events are debounced and reconciled against the changed file's parent,
        // so a burst (e.g. a note save writing once) refreshes once — and only
        // the affected directory, never the whole tree blindly.
        monitor_->signal_changed().connect(
            [this](const Glib::RefPtr<Gio::File>& file,
                   const Glib::RefPtr<Gio::File>& other,
                   Gio::FileMonitor::Event) {
                const auto affected = other ? other : file;
                if (!affected) { schedule_refresh(); return; }
                const std::string p = affected->get_path();
                if (p.empty()) { schedule_refresh(); return; }
                debounce_.disconnect();
                const auto parent = std::filesystem::path(p).parent_path();
                debounce_ = Glib::signal_timeout().connect(
                    [this, parent]() {
                        reconcile_directory(parent);
                        return false;
                    },
                    150);
            });
    } catch (const Glib::Error&) {
        // Directory may be read-only/unmonitorable; the reload button still works.
    }
}

void DirectoryTreePanel::schedule_refresh() {
    // Coalesce bursts of filesystem events into a single rebuild.
    debounce_.disconnect();
    debounce_ = Glib::signal_timeout().connect(
        [this]() {
            refresh();
            return false;
        },
        150);
}

void DirectoryTreePanel::set_root(const std::filesystem::path& dir) {
    state_.current_dir = dir;
    // Navigating to a new root resets only the tree-local state; the search
    // filter (like VS Code) survives navigation. Fresh roots always start at
    // the top of the listing.
    state_.expanded.clear();
    state_.selected.clear();
    state_.anchor.clear();
    if (path_label_) path_label_->set_text(dir.string());
    start_monitor(current_dir());
    refresh_to_top();
}

void DirectoryTreePanel::focus_search() {
    if (search_) search_->grab_focus();
}

bool DirectoryTreePanel::matches_filter(const std::string& name) const {
    if (state_.filter.empty()) return true;
    std::string lower_name = name;
    std::string lower_filter = state_.filter;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);
    return lower_name.find(lower_filter) != std::string::npos;
}

void DirectoryTreePanel::refresh() {
    if (!tree_) return;
    capture_scroll_anchor();
    rebuild_top_level();
    if (force_scroll_top_) {
        // A forced top scroll (Reload / Home / fresh launch) must not restore
        // a previously captured anchor.
        state_.anchor.clear();
        state_.anchor_offset = 0.0;
        force_scroll_top_ = false;
    }
    // Widgets added above are not allocated until the next layout pass, so the
    // scroll restore has to wait for their allocations to exist.
    restore_idle_.disconnect();
    restore_idle_ = Glib::signal_idle().connect([this]() {
        auto vadj = scroller_ ? scroller_->get_vadjustment() : Glib::RefPtr<Gtk::Adjustment>();
        if (!vadj) return false;
        if (state_.anchor.empty()) {
            // A forced top scroll (Reload / Home / fresh launch). Keep waiting
            // on the idle handler until the tree has actually been allocated
            // (upper > 0 means content exists to scroll); otherwise set_value
            // is a no-op because the adjustment has no range yet. Bounded so a
            // hidden/empty tree cannot keep the main loop spinning forever.
            static unsigned wait_rounds = 0;
            if (vadj->get_upper() <= 1.0 && ++wait_rounds < 200) return true;
            wait_rounds = 0;
            vadj->set_value(0.0);
            return false;
        }
        restore_scroll_anchor();
        return false;
    });
}

void DirectoryTreePanel::rebuild_top_level() {
    while (auto* child = tree_->get_first_child()) tree_->remove(*child);

    if (current_dir().has_parent_path()) {
        if (matches_filter("..")) {
            auto* parent_row = create_row("..", true, current_dir().parent_path());
            tree_->append(*parent_row);
        }
    }

    try {
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(current_dir())) {
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            bool a_dir = a.is_directory();
            bool b_dir = b.is_directory();
            if (a_dir != b_dir) return a_dir > b_dir;
            return a.path().filename().string() < b.path().filename().string();
        });

        for (const auto& entry : entries) {
            std::string name = entry.path().filename().string();
            if (!name.empty() && name[0] == '.') continue;
            if (!matches_filter(name)) continue;
            if (entry.is_directory()) {
                tree_->append(*create_row(name, true, entry.path()));
            } else if (entry.is_regular_file()) {
                tree_->append(*create_row(name, false, entry.path()));
            }
        }
    } catch (const std::exception&) {}
}

Gtk::Widget* DirectoryTreePanel::create_row(const std::string& name, bool is_dir, const std::filesystem::path& full_path) {
    auto* container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    set_path(*container, full_path);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    box->add_css_class("remin-directory-row");
    if (is_dir) box->add_css_class("remin-directory-row-dir");
    if (full_path == state_.selected) box->add_css_class("remin-selected");
    box->set_margin_start(8);
    box->set_margin_end(4);
    set_slot(*container, kBoxKey, box);

    const bool is_parent = (name == "..");

    // Expander chevron: only for real subdirectories. The parent ("..") row
    // navigates up instead — no chevron, no inline expand.
    auto* arrow = Gtk::make_managed<Gtk::Image>();
    arrow->set_pixel_size(12);
    if (is_dir && !is_parent) {
        arrow->set_from_icon_name("pan-end-symbolic");
        arrow->add_css_class("remin-dir-arrow");
    } else {
        arrow->set_from_icon_name("emblem-ok-symbolic");
        arrow->set_opacity(0.0);
    }
    arrow->set_valign(Gtk::Align::CENTER);
    box->append(*arrow);
    set_slot(*container, kArrowKey, arrow);

    // Special icons for parent directory (..) and home directory
    std::string icon_name;
    std::string display_name = name;
    bool show_icon = true;
    if (name == "..") {
        // Parent directory: centered "Back .." text, no icon
        display_name = "Back ..";
        show_icon = false;
    } else if (is_dir && full_path == get_home_dir()) {
        icon_name = "user-home-symbolic";  // Home directory
    } else {
        icon_name = is_dir ? "folder-symbolic" : "text-x-generic-symbolic";
    }

    if (show_icon) {
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(icon_name);
        icon->set_pixel_size(14);
        icon->set_valign(Gtk::Align::CENTER);
        box->append(*icon);
    }

    auto* label = Gtk::make_managed<Gtk::Label>(display_name);
    if (name == "..") {
        label->add_css_class("remin-dir-back");
        label->set_halign(Gtk::Align::CENTER);
    } else {
        label->set_halign(Gtk::Align::START);
    }
    label->set_hexpand(true);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    label->add_css_class("remin-directory-name");
    box->append(*label);

    if (!is_dir) {
        try {
            auto ftime = std::filesystem::last_write_time(full_path);
            auto fsize = std::filesystem::file_size(full_path);
            auto* size_label = Gtk::make_managed<Gtk::Label>(format_file_size(fsize));
            size_label->add_css_class("remin-directory-size");
            size_label->set_valign(Gtk::Align::CENTER);
            box->append(*size_label);
            set_slot(*container, kSizeKey, size_label);
            std::string tooltip = name + "\nModified: " + format_file_time(ftime);
            box->set_tooltip_text(tooltip);
        } catch (...) {}
    } else if (!is_parent) {
        // Show how many entries live inside a directory, e.g. "9 items", as a
        // subtle suffix like the file-size suffix on files.
        std::size_t count = 0;
        try {
            count = std::distance(std::filesystem::directory_iterator(full_path),
                                  std::filesystem::directory_iterator());
        } catch (...) {}
        auto* count_label = Gtk::make_managed<Gtk::Label>(
            std::to_string(count) + (count == 1 ? " item" : " items"));
        count_label->add_css_class("remin-directory-size");
        count_label->set_valign(Gtk::Align::CENTER);
        box->append(*count_label);
        set_slot(*container, kSizeKey, count_label);
        box->set_tooltip_text(full_path.string());
    } else {
        box->set_tooltip_text(full_path.string());
    }

    container->append(*box);

    if (is_parent) {
        // Single click on ".." navigates one level up (never expands inline).
        auto click = Gtk::GestureClick::create();
        click->signal_pressed().connect([this, full_path](int n_press, double, double) {
            if (n_press == 1) set_root(full_path);
        });
        box->add_controller(click);
    } else if (is_dir) {
        bool has_children = false;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
                has_children = true;
                break;
            }
        } catch (...) {}

        // children box always exists so reconcile_directory() can target it,
        // even while collapsed (it stays hidden until the user expands).
        auto* children_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        children_box->set_margin_start(14);
        children_box->set_visible(false);
        container->append(*children_box);
        set_slot(*container, kChildrenKey, children_box);

        const bool expanded = state_.expanded.count(full_path) > 0;
        if (expanded && has_children) {
            children_box->set_visible(true);
            arrow->set_from_icon_name("pan-down-symbolic");
            populate_children(children_box, full_path);
        }

        auto click = Gtk::GestureClick::create();
        click->signal_pressed().connect(
            [this, box, arrow, children_box, has_children, full_path](int n_press, double, double) {
                if (n_press == 1) {
                    set_selected(full_path);
                    if (has_children) {
                        bool was = state_.expanded.count(full_path) > 0;
                        if (was) {
                            state_.expanded.erase(full_path);
                            children_box->set_visible(false);
                            arrow->set_from_icon_name("pan-end-symbolic");
                        } else {
                            state_.expanded.insert(full_path);
                            // Always repopulate so expanding shows fresh
                            // contents (a monitor reconcile while collapsed
                            // leaves the hidden box untouched).
                            while (auto* child = children_box->get_first_child())
                                children_box->remove(*child);
                            populate_children(children_box, full_path);
                            children_box->set_visible(true);
                            arrow->set_from_icon_name("pan-down-symbolic");
                        }
                    }
                } else if (n_press == 2) {
                    // Double-click a subdirectory enters it (navigation).
                    set_root(full_path);
                }
            });
        box->add_controller(click);

        auto right_click = Gtk::GestureClick::create();
        right_click->set_button(3);
        right_click->signal_pressed().connect([this, box, full_path, name](int, double x, double y) {
            show_context_menu(*box, full_path, name, true, x, y);
        });
        box->add_controller(right_click);
    } else {
        auto click = Gtk::GestureClick::create();
        click->signal_pressed().connect([this, box, full_path](int n_press, double, double) {
            if (n_press == 1) {
                set_selected(full_path);
            } else if (n_press == 2 && open_file_) {
                open_file_(full_path);
            }
        });
        box->add_controller(click);
        auto right_click = Gtk::GestureClick::create();
        right_click->set_button(3);
        right_click->signal_pressed().connect([this, box, full_path, name](int, double x, double y) {
            show_context_menu(*box, full_path, name, false, x, y);
        });
        box->add_controller(right_click);
    }

    return container;
}

void DirectoryTreePanel::populate_children(Gtk::Box* children_box, const std::filesystem::path& dir_path) {
    try {
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            bool a_dir = a.is_directory();
            bool b_dir = b.is_directory();
            if (a_dir != b_dir) return a_dir > b_dir;
            return a.path().filename().string() < b.path().filename().string();
        });
        for (const auto& entry : entries) {
            std::string name = entry.path().filename().string();
            if (!name.empty() && name[0] == '.') continue;
            if (entry.is_directory()) {
                children_box->append(*create_row(name, true, entry.path()));
            } else if (entry.is_regular_file()) {
                children_box->append(*create_row(name, false, entry.path()));
            }
        }
    } catch (const std::exception&) {}
}

void DirectoryTreePanel::reconcile_directory(const std::filesystem::path& dir) {
    if (dir.empty() || !tree_) return;

    // Top-level change: stateful top-level rebuild keeps expansion/selection/
    // filter/scroll; only the visible root rows are re-created.
    if (dir == current_dir()) {
        refresh();
        return;
    }

    // Nested change: rebuild only the affected directory's rendered children.
    auto* row = find_container(dir);
    if (!row) return;  // not currently rendered (collapsed or out of scope)
    auto* children_box = dynamic_cast<Gtk::Box*>(get_slot(*row, kChildrenKey));
    if (!children_box) return;

    // Keep the rendered children in sync with the expansion state: a directory
    // that is expanded stays expanded (and shows the downward chevron), one
    // that is collapsed stays collapsed.
    const bool expanded = state_.expanded.count(dir) > 0;
    if (expanded && children_box->get_first_child() != nullptr) {
        while (auto* child = children_box->get_first_child()) children_box->remove(*child);
    }
    if (auto* arrow = dynamic_cast<Gtk::Image*>(get_slot(*row, kArrowKey)))
        arrow->set_from_icon_name(expanded ? "pan-down-symbolic" : "pan-end-symbolic");
    children_box->set_visible(expanded);
    if (expanded) {
        populate_children(children_box, dir);
    }
    if (auto* count_w = get_slot(*row, kSizeKey)) {
        std::size_t count = 0;
        try {
            count = std::distance(std::filesystem::directory_iterator(dir),
                                  std::filesystem::directory_iterator());
        } catch (...) {}
        if (auto* count_label = dynamic_cast<Gtk::Label*>(count_w))
            count_label->set_text(std::to_string(count) + (count == 1 ? " item" : " items"));
    }
    apply_selection_visual();
}

void DirectoryTreePanel::update_file_row(const std::filesystem::path& path) {
    auto* row = find_container(path);
    if (!row) return;
    auto* size = get_slot(*row, kSizeKey);
    auto* box = get_slot(*row, kBoxKey);
    if (!size || !box) return;
    try {
        auto ftime = std::filesystem::last_write_time(path);
        auto fsize = std::filesystem::file_size(path);
        auto* label = dynamic_cast<Gtk::Label*>(size);
        if (label) label->set_text(format_file_size(fsize));
        box->set_tooltip_text(path.filename().string() + "\nModified: " + format_file_time(ftime));
    } catch (...) {}
}

void DirectoryTreePanel::on_note_saved(const std::filesystem::path& chosen) {
    if (chosen.empty() || !tree_) return;

    // Already rendered: bump size/mtime in place — no rebuild at all.
    if (find_container(chosen)) {
        update_file_row(chosen);
        return;
    }

    // New path (Save As, rename from host): reconcile only the parent dir so a
    // brand-new row appears and the rest of the tree is untouched.
    const auto parent = chosen.parent_path();
    if (!parent.empty()) reconcile_directory(parent);
}

void DirectoryTreePanel::show_context_menu(Gtk::Widget& widget, const std::filesystem::path& path, const std::string& name, bool is_dir, double x, double y) {
    std::vector<ContextMenu::Item> items;

    if (is_dir) {
        items.push_back({"New File", [this, path]() { create_new_file(path); }});
        items.push_back({"New Folder", [this, path]() { create_new_folder(path); }});
        items.push_back({"Rename", [this, path]() { rename_item(path); }});
        items.push_back({"Delete", [this, path]() { delete_item(path); }});
    } else {
        items.push_back({"Open", [this, path]() { if (open_file_) open_file_(path); }});
        items.push_back({"Rename", [this, path]() { rename_item(path); }});
        items.push_back({"Delete", [this, path]() { delete_item(path); }});
    }
    items.push_back({ContextMenu::SEPARATOR_LABEL, {}});
    items.push_back({"Copy Path", [this, path]() {
        auto display = get_display();
        if (display) display->get_clipboard()->set_text(path.string());
    }});

    ContextMenu::show(widget, x, y, items);
}

void DirectoryTreePanel::create_new_file(const std::filesystem::path& dir) {
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win) return;
    auto dialog = Gtk::make_managed<Gtk::Dialog>("New File", *win, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Create", Gtk::ResponseType::OK);
    dialog->set_default_response(Gtk::ResponseType::OK);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_placeholder_text("filename.txt");
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();
    dialog->signal_response().connect([this, dialog, entry, dir](int response) {
        if (response == Gtk::ResponseType::OK) {
            std::filesystem::path new_path = dir / entry->get_text().raw();
            if (new_path.empty() || entry->get_text().raw().empty()) {
                show_error(*dialog, "File name cannot be empty.");
                return;  // keep dialog open
            }
            if (std::filesystem::exists(new_path)) {
                show_error(*dialog, "A file or folder named \"" + entry->get_text().raw() +
                                    "\" already exists in this folder.");
                return;  // keep dialog open
            }
            std::ofstream file(new_path);
            file.close();
            reveal_after_create(dir, new_path);
        }
        dialog->close();
    });
    dialog->present();
}

void DirectoryTreePanel::create_new_folder(const std::filesystem::path& dir) {
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win) return;
    auto dialog = Gtk::make_managed<Gtk::Dialog>("New Folder", *win, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Create", Gtk::ResponseType::OK);
    dialog->set_default_response(Gtk::ResponseType::OK);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_placeholder_text("folder_name");
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();
    dialog->signal_response().connect([this, dialog, entry, dir](int response) {
        if (response == Gtk::ResponseType::OK) {
            const std::string name = entry->get_text().raw();
            if (name.empty()) {
                show_error(*dialog, "Folder name cannot be empty.");
                return;  // keep dialog open
            }
            std::filesystem::path new_path = dir / name;
            if (std::filesystem::exists(new_path)) {
                show_error(*dialog, "A file or folder named \"" + name +
                                    "\" already exists in this folder.");
                return;  // keep dialog open
            }
            std::filesystem::create_directory(new_path);
            reveal_after_create(dir, new_path);
        }
        dialog->close();
    });
    dialog->present();
}

void DirectoryTreePanel::show_error(Gtk::Window& win, const std::string& message) {
    auto* dlg = Gtk::make_managed<Gtk::MessageDialog>(
        win, "Cannot create", false, Gtk::MessageType::ERROR,
        Gtk::ButtonsType::OK, false);
    dlg->set_secondary_text(message);
    dlg->set_modal(true);
    dlg->signal_response().connect([dlg](int) { dlg->close(); });
    dlg->present();
}

void DirectoryTreePanel::rename_item(const std::filesystem::path& path) {
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win) return;
    auto dialog = Gtk::make_managed<Gtk::Dialog>("Rename", *win, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Rename", Gtk::ResponseType::OK);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_text(path.filename().string());
    entry->select_region(0, -1);
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();
    dialog->signal_response().connect([this, dialog, entry, path](int response) {
        if (response == Gtk::ResponseType::OK) {
            const std::string name = entry->get_text().raw();
            if (name.empty()) {
                show_error(*dialog, "Name cannot be empty.");
                return;  // keep dialog open
            }
            const auto new_path = path.parent_path() / name;
            if (std::filesystem::exists(new_path) && new_path != path) {
                show_error(*dialog, "A file or folder named \"" + name +
                                    "\" already exists in this folder.");
                return;  // keep dialog open
            }
            std::filesystem::rename(path, new_path);
            // Keep path-identity state consistent with the rename.
            if (state_.expanded.count(path)) {
                state_.expanded.erase(path);
                state_.expanded.insert(new_path);
            }
            if (state_.selected == path) state_.selected = new_path;
            if (state_.anchor == path) state_.anchor = new_path;
            reconcile_directory(path.parent_path());
            set_selected(new_path);
            apply_selection_visual();
        }
        dialog->close();
    });
    dialog->present();
}

void DirectoryTreePanel::delete_item(const std::filesystem::path& path) {
    auto* win = dynamic_cast<Gtk::Window*>(get_root());
    if (!win) return;
    auto dialog = Gtk::make_managed<Gtk::Dialog>("Delete", *win, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Delete", Gtk::ResponseType::OK);
    dialog->set_default_response(Gtk::ResponseType::CANCEL);
    auto* label = Gtk::make_managed<Gtk::Label>("Delete " + path.filename().string() + "?");
    dialog->get_content_area()->append(*label);
    dialog->signal_response().connect([this, dialog, path](int response) {
        if (response == Gtk::ResponseType::OK) {
            if (std::filesystem::is_directory(path)) {
                std::filesystem::remove_all(path);
            } else {
                std::filesystem::remove(path);
            }
            // Drop expansion/selection/anchor state for the deleted path (and
            // any descendants).
            const std::string prefix = path.string() + "/";
            for (auto it = state_.expanded.begin(); it != state_.expanded.end();) {
                const std::string p = it->string();
                if (p == path.string() || p.rfind(prefix, 0) == 0) {
                    it = state_.expanded.erase(it);
                } else {
                    ++it;
                }
            }
            const std::string sel = state_.selected.string();
            if (sel == path.string() || sel.rfind(prefix, 0) == 0) state_.selected.clear();
            const std::string anc = state_.anchor.string();
            if (anc == path.string() || anc.rfind(prefix, 0) == 0) state_.anchor.clear();
            reconcile_directory(path.parent_path());
            apply_selection_visual();
        }
        dialog->close();
    });
    dialog->present();
}

void DirectoryTreePanel::set_selected(const std::filesystem::path& path) {
    state_.selected = path;
    apply_selection_visual();
}

void DirectoryTreePanel::apply_selection_visual() {
    if (!tree_) return;
    std::vector<Gtk::Widget*> rows;
    collect_containers(tree_, rows);
    for (auto* row : rows) {
        auto* box = get_slot(*row, kBoxKey);
        if (!box) continue;
        if (get_path(*row) == state_.selected.string()) {
            box->add_css_class("remin-selected");
        } else {
            box->remove_css_class("remin-selected");
        }
    }
}

void DirectoryTreePanel::capture_scroll_anchor() {
    if (!scroller_) { state_.anchor.clear(); return; }
    auto vadj = scroller_->get_vadjustment();
    if (!vadj) { state_.anchor.clear(); return; }
    const double scroll = vadj->get_value();

    double best_y = -1.0;
    std::filesystem::path best_path;
    std::vector<Gtk::Widget*> rows;
    collect_containers(tree_, rows);
    for (auto* row : rows) {
        double x = 0.0, y = 0.0;
        // translate_coordinates needs the row to be realized; rows that are not
        // laid out yet are simply skipped (anchor stays reasonable).
        if (!row->translate_coordinates(*tree_, 0.0, 0.0, x, y)) continue;
        if (y <= scroll + 0.5 && y >= best_y) {
            best_y = y;
            best_path = get_path(*row);
        }
    }
    if (!best_path.empty()) {
        state_.anchor = best_path;
        state_.anchor_offset = scroll - best_y;
    } else {
        state_.anchor.clear();
    }
}

void DirectoryTreePanel::restore_scroll_anchor() {
    if (!scroller_ || state_.anchor.empty()) return;
    auto vadj = scroller_->get_vadjustment();
    if (!vadj) return;

    auto* row = find_container(state_.anchor);
    if (!row) return;
    double x = 0.0, y = 0.0;
    if (!row->translate_coordinates(*tree_, 0.0, 0.0, x, y)) return;

    const double upper = std::max(0.0, vadj->get_upper() - vadj->get_page_size());
    const double target = std::clamp(y + state_.anchor_offset, 0.0, upper);
    vadj->set_value(target);
}

Gtk::Widget* DirectoryTreePanel::find_container(const std::filesystem::path& path) {
    if (!tree_) return nullptr;
    const std::string target = path.string();
    std::vector<Gtk::Widget*> rows;
    collect_containers(tree_, rows);
    for (auto* row : rows) {
        if (get_path(*row) == target) return row;
    }
    return nullptr;
}

void DirectoryTreePanel::collect_containers(Gtk::Widget* parent, std::vector<Gtk::Widget*>& out) {
    for (auto* child = parent->get_first_child(); child; child = child->get_next_sibling()) {
        if (get_slot(*child, kBoxKey)) {
            out.push_back(child);
            if (auto* cb = get_slot(*child, kChildrenKey)) collect_containers(cb, out);
        } else {
            collect_containers(child, out);
        }
    }
}

void DirectoryTreePanel::reveal_after_create(const std::filesystem::path& dir,
                                             const std::filesystem::path& new_path) {
    // Expand the target directory and every ancestor down to the tree root so
    // a rebuild renders the folder where the item was just created — the parent
    // directory "drops down" automatically. In the (edge) case of creating via
    // the ".." row, dir sits above the root and nothing is expanded.
    const auto root = current_dir();
    const std::string root_prefix = root.string() + "/";
    for (auto p = dir; !p.empty() && p != root &&
                        p.string().rfind(root_prefix, 0) == 0;
         p = p.parent_path())
        state_.expanded.insert(p);

    // Full stateful refresh: capture the current scroll anchor, rebuild the
    // top level (create_row honours state_.expanded, so the folder expands),
    // then restore the scroll position — the view does not jump around.
    set_selected(new_path);
    refresh();

    // Make the new row visible, but only if it is currently off-screen; when it
    // already fits in the viewport the scroll position is left untouched.
    reveal_path(new_path, true);
}

void DirectoryTreePanel::reveal_path(const std::filesystem::path& path,
                                     bool only_if_off_screen) {
    if (path.empty() || !scroller_) return;
    reveal_idle_.disconnect();
    reveal_idle_ = Glib::signal_idle().connect([this, path, only_if_off_screen]() {
        auto vadj = scroller_->get_vadjustment();
        if (!vadj) return false;
        auto* row = find_container(path);
        if (!row) return false;
        double x = 0.0, y = 0.0;
        if (!row->translate_coordinates(*tree_, 0.0, 0.0, x, y)) return false;
        if (!row->get_mapped()) return false;

        const double page = vadj->get_page_size();
        const double row_h = static_cast<double>(row->get_allocated_height());
        const double scroll = vadj->get_value();

        // Row is fully inside the viewport already — nothing to do.
        if (only_if_off_screen && y >= scroll && y + row_h <= scroll + page)
            return false;

        const double upper = std::max(0.0, vadj->get_upper() - page);
        // Scroll just enough to bring the row into view with a small margin.
        double target = scroll;
        if (y < scroll) target = y - 8.0;
        else if (y + row_h > scroll + page) target = y + row_h - page + 8.0;
        vadj->set_value(std::clamp(target, 0.0, upper));
        return false;
    });
}

} // namespace remin::gui