#include "gui/window/directory_tree_panel.hpp"
#include "gui/ui/context_menu.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace remin::gui {

namespace {

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

    // Header: search box + reload button (top), path label (bottom)
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);

    // Top row: search entry + reload button
    auto* header_top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);

    search_ = Gtk::make_managed<Gtk::SearchEntry>();
    search_->add_css_class("remin-dir-search");
    search_->set_placeholder_text("Search files...");
    search_->set_hexpand(true);
    search_->signal_changed().connect([this]() {
        filter_ = search_->get_text();
        refresh();
    });

    // Reload button: transparent, icon-only, small
    auto* reload = Gtk::make_managed<Gtk::Button>();
    reload->add_css_class("remin-dir-reload");
    reload->set_has_frame(false);
    reload->set_icon_name("view-refresh-symbolic");
    reload->set_tooltip_text("Reload files");
    reload->signal_clicked().connect([this]() { refresh(); });

    // Home button: transparent, icon-only, small (same style as reload)
    auto* home = Gtk::make_managed<Gtk::Button>();
    home->add_css_class("remin-dir-reload");
    home->set_has_frame(false);
    home->set_icon_name("user-home-symbolic");
    home->set_tooltip_text("Go to home directory");
    home->signal_clicked().connect([this]() {
        const char* home_dir = std::getenv("HOME");
        if (home_dir) set_root(home_dir);
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
}

void DirectoryTreePanel::start_monitor(const std::filesystem::path& dir) {
    monitor_.reset();
    try {
        auto file = Gio::File::create_for_path(dir.string());
        monitor_ = file->monitor_directory(Gio::FileMonitorFlags::NONE);
        // Fires on create/delete/rename/content-change; events are debounced
        // so a burst (e.g. a note save writing once) refreshes once.
        monitor_->signal_changed().connect([this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&, Gio::FileMonitor::Event) {
            schedule_refresh();
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
    current_dir_ = dir;
    // Update path label
    if (path_label_) {
        path_label_->set_text(dir.string());
    }
    start_monitor(current_dir_);
    refresh();
}

void DirectoryTreePanel::focus_search() {
    if (search_) search_->grab_focus();
}

bool DirectoryTreePanel::matches_filter(const std::string& name) const {
    if (filter_.empty()) return true;
    std::string lower_name = name;
    std::string lower_filter = filter_;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);
    return lower_name.find(lower_filter) != std::string::npos;
}

void DirectoryTreePanel::refresh() {
    if (!tree_) return;
    while (auto* child = tree_->get_first_child()) tree_->remove(*child);

    if (current_dir_.has_parent_path()) {
        if (matches_filter("..")) {
            auto* parent_row = create_row("..", true, current_dir_.parent_path());
            tree_->append(*parent_row);
        }
    }

    try {
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(current_dir_)) {
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

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    box->add_css_class("remin-directory-row");
    if (is_dir) box->add_css_class("remin-directory-row-dir");
    box->set_margin_start(8);
    box->set_margin_end(4);

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
            std::string tooltip = name + "\nModified: " + format_file_time(ftime);
            box->set_tooltip_text(tooltip);
        } catch (...) {}
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

        // No context menu for "Back .." - it's a navigation element, not a folder
        // auto right_click = Gtk::GestureClick::create();
        // right_click->set_button(3);
        // right_click->signal_pressed().connect([this, box, full_path, name](int, double, double) {
        //     show_context_menu(*box, full_path, name, true);
        // });
        // box->add_controller(right_click);
    } else if (is_dir) {
        bool has_children = false;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
                has_children = true;
                break;
            }
        } catch (...) {}

        if (has_children) {
            auto* children_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
            children_box->set_margin_start(14);
            children_box->set_visible(false);
            container->append(*children_box);

            auto* state = new std::pair<Gtk::Box*, bool>(children_box, false);
            auto click = Gtk::GestureClick::create();
            click->signal_pressed().connect(
                [this, box, arrow, children_box, state, full_path](int n_press, double, double) {
                    if (n_press == 1) {
                        state->second = !state->second;
                        children_box->set_visible(state->second);
                        arrow->set_from_icon_name(state->second ? "pan-down-symbolic" : "pan-end-symbolic");
                        if (state->second && children_box->get_first_child() == nullptr) {
                            populate_children(children_box, full_path);
                        }
                    } else if (n_press == 2) {
                        // Double-click a subdirectory enters it (navigation).
                        set_root(full_path);
                    }
                });
            box->add_controller(click);
        } else {
            // Empty directory: nothing to expand, double-click to enter it.
            auto click = Gtk::GestureClick::create();
            click->signal_pressed().connect([this, full_path](int n_press, double, double) {
                if (n_press == 2) set_root(full_path);
            });
            box->add_controller(click);
        }

        auto right_click = Gtk::GestureClick::create();
        right_click->set_button(3);
        right_click->signal_pressed().connect([this, box, full_path, name](int, double x, double y) {
            show_context_menu(*box, full_path, name, true, x, y);
        });
        box->add_controller(right_click);
    } else {
        auto click = Gtk::GestureClick::create();
        click->signal_pressed().connect([this, full_path](int n_press, double, double) {
            if (n_press == 2 && open_file_) open_file_(full_path);
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
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_placeholder_text("filename.txt");
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();
    dialog->signal_response().connect([this, dialog, entry, dir](int response) {
        if (response == Gtk::ResponseType::OK) {
            std::filesystem::path new_path = dir / entry->get_text().raw();
            std::ofstream file(new_path);
            file.close();
            refresh();
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
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_placeholder_text("folder_name");
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();
    dialog->signal_response().connect([this, dialog, entry, dir](int response) {
        if (response == Gtk::ResponseType::OK) {
            std::filesystem::create_directory(dir / entry->get_text().raw());
            refresh();
        }
        dialog->close();
    });
    dialog->present();
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
            std::filesystem::rename(path, path.parent_path() / entry->get_text().raw());
            refresh();
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
            refresh();
        }
        dialog->close();
    });
    dialog->present();
}

} // namespace remin::gui
