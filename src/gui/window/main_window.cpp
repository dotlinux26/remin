#include "gui/window/main_window.hpp"
#include "gui/window/settings_dialog.hpp"
#include "terminal/shell/shell.hpp"

#include <fstream>
#include <filesystem>
#include <gtkmm.h>
#include <giomm/menu.h>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace remin::gui {

MainWindow::MainWindow(SessionController* controller,
                       remin::core::Autosaver* autosaver,
                       remin::core::WorkspaceCore* core)
    : controller_(controller), autosaver_(autosaver), core_(core) {
    set_title("Remin");
    set_default_size(1024, 768);

    // Register bundled icon theme
    try {
        auto icon_theme = Gtk::IconTheme::get_for_display(get_display());
        icon_theme->add_search_path(std::string(REMIN_RESOURCE_DIR) + "/icons");
    } catch (const Glib::Error&) {}
    set_icon_name("remin");

    // Root overlay for autosave badge
    overlay_ = Gtk::make_managed<Gtk::Overlay>();
    set_child(*overlay_);

    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    overlay_->set_child(*root);

    // Autosave badge (top-right pill)
    autosave_badge_ = Gtk::make_managed<Gtk::Label>();
    autosave_badge_->set_name("autosave-badge");
    autosave_badge_->set_margin_top(8);
    autosave_badge_->set_margin_end(8);
    autosave_badge_->set_halign(Gtk::Align::END);
    autosave_badge_->set_valign(Gtk::Align::START);
    autosave_badge_->set_visible(false);
    overlay_->add_overlay(*autosave_badge_);

    setup_header();
    setup_menu_bar();
    setup_toolbar();
    setup_tab_bar();
    setup_sidebar();
    setup_content_stack();
    setup_status_bar();
    setup_find_bar();

    root->append(*toolbar_);
    root->append(*find_bar_);
    root->append(*tab_bar_);
    root->append(*main_paned_);
    root->append(*status_bar_);

    // Global key controller for accelerators
    key_ctrl_ = Gtk::EventControllerKey::create();
    key_ctrl_->signal_key_pressed().connect(
        sigc::slot<bool(unsigned int, unsigned int, Gdk::ModifierType)>(
            [this](unsigned int keyval, unsigned int, Gdk::ModifierType mods) -> bool {
                return on_find_key_pressed(keyval, 0, mods);
            }),
        false);
    add_controller(key_ctrl_);

    // Click outside find bar to close it
    auto click_ctrl = Gtk::GestureClick::create();
    click_ctrl->set_button(0); // Any button
    click_ctrl->signal_pressed().connect([this](int, double x, double y) {
        if (find_bar_ && find_bar_->get_visible()) {
            // Check if click is outside find_bar
            auto allocation = find_bar_->get_allocation();
            // Get click position relative to find_bar
            auto* root_widget = get_child();
            if (root_widget) {
                // Convert root coordinates to find_bar coordinates
                int fx = allocation.get_x();
                int fy = allocation.get_y();
                int fw = allocation.get_width();
                int fh = allocation.get_height();
                // Click position (x, y) is relative to the window root
                // We need to check if it's within the find_bar rect
                // The click coordinates are in window coordinates
                if (x < fx || x > fx + fw || y < fy || y > fy + fh) {
                    hide_find_bar();
                }
            }
        }
    });
    add_controller(click_ctrl);

    // Open initial terminal tab
    new_terminal_tab();

    // Connect tab switching signal via notify on visible-child-name property
    content_stack_->property_visible_child_name().signal_changed().connect(
        [this]() {
            auto* child = content_stack_->get_visible_child();
            if (!child) return;
            for (size_t i = 0; i < tabs_.size(); ++i) {
                if (tabs_[i].get() == child) {
                    active_tab_ = static_cast<int>(i);
                    tabs_[i]->activate();
                    break;
                }
            }
            update_tab_bar();
            update_toolbar();
            update_status_bar();
        });

    update_header();
    update_tab_bar();
    update_status_bar();
}

MainWindow::~MainWindow() = default;

void MainWindow::setup_header() {
    header_ = Gtk::make_managed<Gtk::HeaderBar>();
    set_titlebar(*header_);

    header_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    header_box_->set_halign(Gtk::Align::CENTER);
    header_box_->set_valign(Gtk::Align::CENTER);

    const std::string logo_path = std::string(REMIN_RESOURCE_DIR) + "/logo.svg";
    try {
        auto tex = Gdk::Texture::create_from_filename(logo_path);
        logo_image_ = Gtk::make_managed<Gtk::Image>(tex);
        logo_image_->set_pixel_size(22);
    } catch (const Glib::Error&) {}

    if (logo_image_) header_box_->append(*logo_image_);

    header_label_ = Gtk::make_managed<Gtk::Label>("Remin");
    header_label_->add_css_class("remin-header-label");
    header_box_->append(*header_label_);

    header_->set_title_widget(*header_box_);
}

void MainWindow::setup_menu_bar() {
    // Window menu — items reference win.<name>, matching the "win" action group.
    auto window_menu = Gio::Menu::create();
    window_menu->append("New Terminal Tab", "win.new");
    window_menu->append("New Note Tab", "win.new_note");
    window_menu->append_section(Gio::Menu::create()); // separator
    window_menu->append("Rename Window…", "win.rename");
    window_menu->append("Close Window", "win.close");
    window_menu->append_section(Gio::Menu::create()); // separator
    window_menu->append("Quit", "app.quit");

    // Terminal menu
    auto terminal_menu = Gio::Menu::create();
    terminal_menu->append("Split Horizontally", "win.split_h");
    terminal_menu->append("Split Vertically", "win.split_v");
    terminal_menu->append("Custom Split…", "win.custom_split");
    terminal_menu->append_section(Gio::Menu::create()); // separator
    terminal_menu->append("Close Pane", "win.close_pane");
    terminal_menu->append_section(Gio::Menu::create()); // separator
    terminal_menu->append("Find…", "win.find");
    terminal_menu->append("Color Profile…", "win.colors");

    // Note menu
    auto note_menu = Gio::Menu::create();
    note_menu->append("Save", "win.save");
    note_menu->append("Save As…", "win.save_as");
    note_menu->append("Toggle Preview", "win.preview");
    note_menu->append_section(Gio::Menu::create()); // separator
    note_menu->append("Find / Replace…", "win.find");

    // View menu
    auto view_menu = Gio::Menu::create();
    view_menu->append("Toggle Dark Theme", "win.dark");
    view_menu->append("Reload Theme", "win.reload");
    view_menu->append_section(Gio::Menu::create()); // separator
    view_menu->append("Auto-Reload Changed Files", "win.autoreload");
    view_menu->append_section(Gio::Menu::create()); // separator
    view_menu->append("Settings…", "win.settings");

    // History menu
    auto history_menu = Gio::Menu::create();
    history_menu->append("Show History Sidebar", "win.toggle_history");
    history_menu->append("Clear History", "win.clear_history");

    auto menu_model = Gio::Menu::create();
    menu_model->append_submenu("Window", window_menu);
    menu_model->append_submenu("Terminal", terminal_menu);
    menu_model->append_submenu("Note", note_menu);
    menu_model->append_submenu("History", history_menu);
    menu_model->append_submenu("View", view_menu);

    menu_bar_ = Gtk::make_managed<Gtk::PopoverMenuBar>();
    menu_bar_->set_menu_model(menu_model);
    header_->pack_start(*menu_bar_);

    // Action group — registered unprefixed, grouped under the "win" prefix so
    // the menu items "win.<name>" resolve to these.
    auto actions = Gio::SimpleActionGroup::create();
    actions->add_action("new", sigc::mem_fun(*this, &MainWindow::new_terminal_tab));
    actions->add_action("new_note", sigc::mem_fun(*this, &MainWindow::new_note_tab));
    actions->add_action("close", [this]() { close(); });
    actions->add_action("rename", sigc::mem_fun(*this, &MainWindow::on_rename_window));
    actions->add_action("split_h", sigc::mem_fun(*this, &MainWindow::on_split_terminal_horizontal));
    actions->add_action("split_v", sigc::mem_fun(*this, &MainWindow::on_split_terminal_vertical));
    actions->add_action("custom_split", [this]() { on_custom_split(); });
    actions->add_action("close_pane", sigc::mem_fun(*this, &MainWindow::on_close_terminal_pane));
    actions->add_action("find", [this]() { show_find_bar(false); });
    actions->add_action("replace", [this]() { show_find_bar(true); });
    actions->add_action("colors", sigc::mem_fun(*this, &MainWindow::on_terminal_color_profile));
    actions->add_action("save", [this]() {
        if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
            tabs_[active_tab_]->kind() == TabKind::Note) {
            static_cast<NoteTabView*>(tabs_[active_tab_].get())->save_now();
        }
    });
    actions->add_action("save_as", [this]() { on_note_save_as(); });
    actions->add_action("preview", sigc::mem_fun(*this, &MainWindow::on_toggle_note_preview));
    actions->add_action("dark", [this]() {
        if (theme_) { theme_->toggle(); refresh_theme(); }
    });
    actions->add_action("reload", [this]() {
        if (theme_) { theme_->apply(theme_->is_dark()); refresh_theme(); }
    });
    {
        auto a = Gio::SimpleAction::create(
            "autoreload",
            Glib::VariantType(Glib::VARIANT_TYPE_BOOL),
            Glib::Variant<bool>::create(controller_ && controller_->auto_reload_enabled()));
        a->signal_activate().connect([this](const Glib::VariantBase& state) {
            if (controller_) {
                auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(state);
                if (b) controller_->set_auto_reload_enabled(b.get());
            }
        });
        actions->add_action(a);
    }
    actions->add_action("toggle_history", [this]() { toggle_history_sidebar(); });
    actions->add_action("clear_history", [this]() { clear_history(); });
    actions->add_action("settings", [this]() { on_settings(); });
    insert_action_group("win", actions);
}

void MainWindow::setup_toolbar() {
    toolbar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    toolbar_->add_css_class("remin-toolbar");
    toolbar_->set_margin_top(2);
    toolbar_->set_margin_bottom(2);
    toolbar_->set_margin_start(8);
    toolbar_->set_margin_end(8);
    update_toolbar();
}

namespace {
Gtk::Button* make_tool_btn(const char* icon, const char* label, const char* tip,
                           std::function<void()> on_click) {
    auto* b = Gtk::make_managed<Gtk::Button>();
    b->add_css_class("remin-tool-btn");
    b->set_tooltip_text(tip);

    // Create a box with icon and label
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* img = Gtk::make_managed<Gtk::Image>(icon);
    img->set_pixel_size(16);
    auto* lbl = Gtk::make_managed<Gtk::Label>(label);
    lbl->add_css_class("remin-tool-label");
    box->append(*img);
    box->append(*lbl);
    b->set_child(*box);

    b->signal_clicked().connect(std::move(on_click));
    return b;
}
} // namespace

void MainWindow::update_toolbar() {
    if (!toolbar_) return;
    // Clear any existing action buttons (children),
    while (auto* child = toolbar_->get_first_child()) toolbar_->remove(*child);

    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;

    if (tabs_[active_tab_]->kind() == TabKind::Terminal) {
        toolbar_->append(*make_tool_btn(
            "pan-start-symbolic", "Split H", "Split Horizontal (Shift+H)",
            sigc::mem_fun(*this, &MainWindow::on_split_terminal_horizontal)));
        toolbar_->append(*make_tool_btn(
            "pan-end-symbolic", "Split V", "Split Vertical (Shift+V)",
            sigc::mem_fun(*this, &MainWindow::on_split_terminal_vertical)));
        toolbar_->append(*make_tool_btn(
            "window-close-symbolic", "Close", "Close Pane",
            sigc::mem_fun(*this, &MainWindow::on_close_terminal_pane)));
        toolbar_->append(*make_tool_btn(
            "edit-find-symbolic", "Find", "Find (Ctrl+F)",
            [this]() { show_find_bar(false); }));
    } else if (tabs_[active_tab_]->kind() == TabKind::Note) {
        toolbar_->append(*make_tool_btn(
            "document-save-symbolic", "Save", "Save (Ctrl+S)",
            [this]() {
                if (auto* n = dynamic_cast<NoteTabView*>(tabs_[active_tab_].get()))
                    n->save_now();
            }));
        toolbar_->append(*make_tool_btn(
            "document-save-as-symbolic", "Save As", "Save As…",
            [this]() { on_note_save_as(); }));
        toolbar_->append(*make_tool_btn(
            "view-paged-symbolic", "Preview", "Toggle Preview",
            sigc::mem_fun(*this, &MainWindow::on_toggle_note_preview)));
        toolbar_->append(*make_tool_btn(
            "edit-find-replace-symbolic", "Replace", "Find / Replace (Ctrl+H)",
            [this]() { show_find_bar(true); }));
    }
}

void MainWindow::setup_tab_bar() {
    tab_bar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    tab_bar_->add_css_class("remin-tab-bar");
    tab_bar_->set_margin_top(4);
    tab_bar_->set_margin_bottom(4);
    tab_bar_->set_margin_start(8);
    tab_bar_->set_margin_end(8);

    new_terminal_btn_ = Gtk::make_managed<Gtk::Button>();
    new_terminal_btn_->add_css_class("remin-tab-btn");
    new_terminal_btn_->set_icon_name("list-add-symbolic");
    new_terminal_btn_->set_tooltip_text("New Terminal Tab (Ctrl+T)");
    new_terminal_btn_->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::new_terminal_tab));
    tab_bar_->append(*new_terminal_btn_);

    new_note_btn_ = Gtk::make_managed<Gtk::Button>();
    new_note_btn_->add_css_class("remin-tab-btn");
    new_note_btn_->set_icon_name("document-new-symbolic");
    new_note_btn_->set_tooltip_text("New Note Tab (Ctrl+Shift+N)");
    new_note_btn_->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::new_note_tab));
    tab_bar_->append(*new_note_btn_);
}

void MainWindow::setup_content_stack() {
    content_stack_ = Gtk::make_managed<Gtk::Stack>();
    content_stack_->set_hexpand(true);
    content_stack_->set_vexpand(true);
    content_stack_->set_transition_type(Gtk::StackTransitionType::SLIDE_LEFT_RIGHT);

    // Main paned: [sidebar | content_stack]
    main_paned_ = Gtk::make_managed<Gtk::Paned>();
    main_paned_->set_orientation(Gtk::Orientation::HORIZONTAL);
    main_paned_->set_hexpand(true);
    main_paned_->set_vexpand(true);
    main_paned_->set_wide_handle(true);
    main_paned_->set_start_child(*sidebar_stack_);
    main_paned_->set_end_child(*content_stack_);
}

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
} // anonymous namespace

void MainWindow::setup_sidebar() {
    // Tab switcher: History | Directory
    auto* tab_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    tab_bar->add_css_class("remin-sidebar-tabs");

    auto* history_tab = Gtk::make_managed<Gtk::Button>("History");
    history_tab->add_css_class("remin-sidebar-tab");
    history_tab->set_hexpand(true);
    history_tab->signal_clicked().connect([this]() { set_sidebar_mode("history"); });

    auto* directory_tab = Gtk::make_managed<Gtk::Button>("Files");
    directory_tab->add_css_class("remin-sidebar-tab");
    directory_tab->set_hexpand(true);
    directory_tab->signal_clicked().connect([this]() { set_sidebar_mode("directory"); });

    tab_bar->append(*history_tab);
    tab_bar->append(*directory_tab);

    // Sidebar stack containing [tab_bar + history | directory]
    sidebar_stack_ = Gtk::make_managed<Gtk::Stack>();
    sidebar_stack_->set_hexpand(false);
    sidebar_stack_->set_vexpand(true);

    auto* history_container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    history_container->append(*tab_bar);
    history_scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    history_scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    history_scroller_->set_vexpand(true);
    history_list_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    history_scroller_->set_child(*history_list_);
    history_container->append(*history_scroller_);
    sidebar_stack_->add(*history_container, "history", "History");

    auto* directory_container = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    auto* dir_tab_bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    dir_tab_bar->add_css_class("remin-sidebar-tabs");
    auto* dir_tab_label = Gtk::make_managed<Gtk::Label>("Files");
    dir_tab_label->add_css_class("remin-sidebar-tab");
    dir_tab_label->set_hexpand(true);
    dir_tab_bar->append(*dir_tab_label);
    directory_container->append(*dir_tab_bar);

    directory_search_ = Gtk::make_managed<Gtk::SearchEntry>();
    directory_search_->add_css_class("remin-dir-search");
    directory_search_->set_placeholder_text("Search files...");
    directory_search_->set_hexpand(true);
    directory_search_->signal_changed().connect([this]() {
        directory_filter_ = directory_search_->get_text();
        refresh_directory();
    });
    directory_container->append(*directory_search_);

    directory_scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    directory_scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    directory_scroller_->set_vexpand(true);
    directory_tree_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    directory_scroller_->set_child(*directory_tree_);
    directory_container->append(*directory_scroller_);
    sidebar_stack_->add(*directory_container, "directory", "Files");

    const char* home = std::getenv("HOME");
    current_dir_ = home ? home : "/";

    // Load persisted command history
    if (controller_) {
        auto persisted = controller_->get_command_history();
        for (auto& cmd : persisted) {
            history_.push_back(std::move(cmd));
        }
    }
    update_history_sidebar();
    set_sidebar_mode("history");
}

void MainWindow::set_sidebar_mode(const std::string& mode) {
    if (!sidebar_stack_) return;
    if (mode == "history") {
        sidebar_stack_->set_visible_child("history");
    } else if (mode == "directory") {
        sidebar_stack_->set_visible_child("directory");
        refresh_directory();
    }
}

void MainWindow::update_history_sidebar() {
    if (!history_list_) return;
    while (auto* child = history_list_->get_first_child()) history_list_->remove(*child);

    auto const add = [this](const std::string& cmd, guint index) {
        auto* btn = Gtk::make_managed<Gtk::Button>(cmd);
        btn->add_css_class("remin-history-item");
        btn->set_halign(Gtk::Align::FILL);
        btn->signal_clicked().connect([this, index]() {
            std::string c = history_[index];
            if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
                tabs_[active_tab_]->kind() == TabKind::Terminal) {
                auto* t = static_cast<TerminalTabView*>(tabs_[active_tab_].get());
                if (auto* p = t->focused_pane()) p->feed(c);
            }
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

void MainWindow::clear_history() {
    history_.clear();
    update_history_sidebar();
}

bool MainWindow::directory_matches_filter(const std::string& name) {
    if (directory_filter_.empty()) return true;
    std::string lower_name = name;
    std::string lower_filter = directory_filter_;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);
    return lower_name.find(lower_filter) != std::string::npos;
}

void MainWindow::refresh_directory() {
    if (!directory_tree_) return;
    while (auto* child = directory_tree_->get_first_child()) directory_tree_->remove(*child);

    if (current_dir_.has_parent_path()) {
        if (directory_matches_filter("..")) {
            auto* parent_row = create_directory_row("..", true, current_dir_.parent_path());
            directory_tree_->append(*parent_row);
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
            if (!directory_matches_filter(name)) continue;
            if (entry.is_directory()) {
                directory_tree_->append(*create_directory_row(name, true, entry.path()));
            } else if (entry.is_regular_file()) {
                directory_tree_->append(*create_directory_row(name, false, entry.path()));
            }
        }
    } catch (const std::exception&) {}
}

Gtk::Widget* MainWindow::create_directory_row(const std::string& name, bool is_dir, const std::filesystem::path& full_path) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    box->add_css_class("remin-directory-row");
    box->set_margin_start(8);
    box->set_margin_end(4);

    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name(is_dir ? "folder-symbolic" : "text-x-generic-symbolic");
    icon->set_pixel_size(14);
    icon->set_valign(Gtk::Align::CENTER);
    box->append(*icon);

    auto* label = Gtk::make_managed<Gtk::Label>(name);
    label->set_halign(Gtk::Align::START);
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

    if (is_dir) {
        bool has_children = false;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
                has_children = true;
                break;
            }
        } catch (...) {}

        if (has_children) {
            auto* expander = Gtk::make_managed<Gtk::Expander>();
            expander->set_child(*box);
            expander->set_expanded(false);
            expander->property_expanded().signal_changed().connect([this, expander, full_path]() {
                if (expander->get_expanded()) {
                    populate_directory_expander(expander, full_path);
                }
            });
            auto click = Gtk::GestureClick::create();
            click->set_button(3);
            click->signal_pressed().connect([this, expander, full_path, name](int, double, double) {
                show_directory_context_menu(*expander, full_path, name, true);
            });
            expander->add_controller(click);
            return expander;
        } else {
            auto right_click = Gtk::GestureClick::create();
            right_click->set_button(3);
            right_click->signal_pressed().connect([this, box, full_path, name](int, double, double) {
                show_directory_context_menu(*box, full_path, name, true);
            });
            box->add_controller(right_click);
            return box;
        }
    } else {
        auto click = Gtk::GestureClick::create();
        click->signal_pressed().connect([this, full_path, name](int n_press, double, double) {
            if (n_press == 2) {
                open_note_from_path(full_path);
            }
        });
        box->add_controller(click);
        auto right_click = Gtk::GestureClick::create();
        right_click->set_button(3);
        right_click->signal_pressed().connect([this, box, full_path, name](int, double, double) {
            show_directory_context_menu(*box, full_path, name, false);
        });
        box->add_controller(right_click);
        return box;
    }
}

void MainWindow::populate_directory_expander(Gtk::Expander* expander, const std::filesystem::path& dir_path) {
    Gtk::Widget* child = expander->get_child();
    Gtk::Box* box = dynamic_cast<Gtk::Box*>(child);
    if (!box) return;

    Gtk::Box* children_box = nullptr;
    for (auto* w = box->get_first_child(); w; w = w->get_next_sibling()) {
        if (auto* b = dynamic_cast<Gtk::Box*>(w)) {
            if (b->get_orientation() == Gtk::Orientation::VERTICAL) {
                children_box = b;
                break;
            }
        }
    }
    if (!children_box) {
        children_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        children_box->set_margin_start(16);
        box->append(*children_box);
    } else {
        while (auto* c = children_box->get_first_child()) children_box->remove(*c);
    }

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
                children_box->append(*create_directory_row(name, true, entry.path()));
            } else if (entry.is_regular_file()) {
                children_box->append(*create_directory_row(name, false, entry.path()));
            }
        }
    } catch (const std::exception&) {}
}

void MainWindow::show_directory_context_menu(Gtk::Widget& widget, const std::filesystem::path& path, const std::string& name, bool is_dir) {
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

    if (is_dir) {
        add_item("New File", [this, path]() { create_new_file(path); });
        add_item("New Folder", [this, path]() { create_new_folder(path); });
        add_item("Rename", [this, path]() { rename_item(path); });
        add_item("Delete", [this, path]() { delete_item(path); });
    } else {
        add_item("Open", [this, path]() { open_note_from_path(path); });
        add_item("Rename", [this, path]() { rename_item(path); });
        add_item("Delete", [this, path]() { delete_item(path); });
    }
    add_item("Copy Path", [this, path]() {
        auto display = get_display();
        if (display) display->get_clipboard()->set_text(path.string());
    });

    menu->set_child(*box);
    menu->set_parent(widget);
    menu->popup();
}

void MainWindow::create_new_file(const std::filesystem::path& dir) {
    auto dialog = Gtk::make_managed<Gtk::Dialog>("New File", *this, true);
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
            refresh_directory();
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::create_new_folder(const std::filesystem::path& dir) {
    auto dialog = Gtk::make_managed<Gtk::Dialog>("New Folder", *this, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Create", Gtk::ResponseType::OK);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_placeholder_text("folder_name");
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();
    dialog->signal_response().connect([this, dialog, entry, dir](int response) {
        if (response == Gtk::ResponseType::OK) {
            std::filesystem::create_directory(dir / entry->get_text().raw());
            refresh_directory();
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::rename_item(const std::filesystem::path& path) {
    auto dialog = Gtk::make_managed<Gtk::Dialog>("Rename", *this, true);
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
            refresh_directory();
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::delete_item(const std::filesystem::path& path) {
    auto dialog = Gtk::make_managed<Gtk::Dialog>("Delete", *this, true);
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
            refresh_directory();
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::setup_status_bar() {
    status_bar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    status_bar_->add_css_class("remin-status-bar");
    status_bar_->set_margin_start(12);
    status_bar_->set_margin_end(12);
    status_bar_->set_margin_top(4);
    status_bar_->set_margin_bottom(4);

    status_label_ = Gtk::make_managed<Gtk::Label>("");
    status_label_->add_css_class("remin-status");
    status_label_->set_xalign(0.0);
    status_bar_->append(*status_label_);
}

void MainWindow::setup_find_bar() {
    find_bar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    find_bar_->add_css_class("remin-find-bar");
    find_bar_->set_margin_top(2);
    find_bar_->set_margin_bottom(2);
    find_bar_->set_margin_start(8);
    find_bar_->set_margin_end(8);
    find_bar_->set_visible(false);

    auto* find_icon = Gtk::make_managed<Gtk::Image>("edit-find-symbolic");
    find_icon->set_pixel_size(16);
    find_entry_ = Gtk::make_managed<Gtk::Entry>();
    find_entry_->set_hexpand(true);
    find_entry_->set_placeholder_text("Search…");
    find_entry_->signal_activate().connect([this]() { on_find_next(); });

    auto* replace_icon = Gtk::make_managed<Gtk::Image>("edit-find-replace-symbolic");
    replace_icon->set_pixel_size(16);
    replace_entry_ = Gtk::make_managed<Gtk::Entry>();
    replace_entry_->set_hexpand(true);
    replace_entry_->set_placeholder_text("Replace with…");
    replace_entry_->set_visible(false);
    replace_icon->set_visible(false);

    find_prev_ = Gtk::make_managed<Gtk::Button>();
    find_prev_->set_child(*Gtk::make_managed<Gtk::Image>("go-up-symbolic"));
    find_prev_->set_tooltip_text("Previous (Shift+Enter)");
    find_prev_->signal_clicked().connect([this]() { on_find_prev(); });

    find_next_ = Gtk::make_managed<Gtk::Button>();
    find_next_->set_child(*Gtk::make_managed<Gtk::Image>("go-down-symbolic"));
    find_next_->set_tooltip_text("Next (Enter)");
    find_next_->signal_clicked().connect([this]() { on_find_next(); });

    replace_btn_ = Gtk::make_managed<Gtk::Button>("Replace");
    replace_btn_->set_visible(false);
    replace_btn_->signal_clicked().connect([this]() { on_replace(); });

    replace_all_btn_ = Gtk::make_managed<Gtk::Button>("Replace All");
    replace_all_btn_->set_visible(false);
    replace_all_btn_->signal_clicked().connect([this]() { on_replace_all(); });

    find_close_ = Gtk::make_managed<Gtk::Button>();
    find_close_->set_child(*Gtk::make_managed<Gtk::Image>("window-close-symbolic"));
    find_close_->set_tooltip_text("Close (Esc)");
    find_close_->signal_clicked().connect([this]() { hide_find_bar(); });

    find_bar_->append(*find_icon);
    find_bar_->append(*find_entry_);
    find_bar_->append(*replace_icon);
    find_bar_->append(*replace_entry_);
    find_bar_->append(*find_prev_);
    find_bar_->append(*find_next_);
    find_bar_->append(*replace_btn_);
    find_bar_->append(*replace_all_btn_);
    find_bar_->append(*find_close_);

    // Store icon pointers for visibility toggling
    find_icon_ = find_icon;
    replace_icon_ = replace_icon;

    find_bar_->set_visible(false);
}

void MainWindow::new_terminal_tab() {
    auto tab_info = controller_->new_terminal_tab("terminal");
    if (tab_info.tab.empty()) return;

    auto* view = new TerminalTabView(controller_, tab_info.window, tab_info.tab, tab_info.root_pane);
    view->set_color_request_callback([this]() { on_terminal_color_profile(); });
    view->set_open_file_callback([this](const std::filesystem::path& path) {
        open_note_from_path(path);
    });
    view->set_history_callback([this](const std::string& cmd) {
        history_.push_back(cmd);
        // Avoid unbounded growth
        if (history_.size() > 2000) history_.erase(history_.begin(), history_.begin() + (history_.size() - 1000));
        update_history_sidebar();
    });
    auto idx = static_cast<int>(tabs_.size());
    tabs_.push_back(std::unique_ptr<TabView>(view));
    term_tabs_.push_back(view);

    content_stack_->add(*view, std::to_string(idx));
    active_tab_ = idx;
    content_stack_->set_visible_child(*view);
    update_tab_bar();
    update_toolbar();
    view->activate();
    update_status_bar();
}

void MainWindow::new_note_tab() {
    auto noteId = controller_->new_note();
    if (noteId.empty()) return;

    auto* view = new NoteTabView(controller_, noteId);
    auto idx = static_cast<int>(tabs_.size());
    tabs_.push_back(std::unique_ptr<TabView>(view));
    note_tabs_.push_back(view);

    content_stack_->add(*view, std::to_string(idx));
    active_tab_ = idx;
    content_stack_->set_visible_child(*view);
    update_tab_bar();
    update_toolbar();
    view->activate();
    update_status_bar();
}

void MainWindow::close_tab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;

    auto kind = tabs_[index]->kind();
    if (kind == TabKind::Terminal) {
        // Remove from term_tabs_
        auto it = std::find(term_tabs_.begin(), term_tabs_.end(),
                           static_cast<TerminalTabView*>(tabs_[index].get()));
        if (it != term_tabs_.end()) term_tabs_.erase(it);
    } else if (kind == TabKind::Note) {
        auto it = std::find(note_tabs_.begin(), note_tabs_.end(),
                           static_cast<NoteTabView*>(tabs_[index].get()));
        if (it != note_tabs_.end()) note_tabs_.erase(it);
    }

    tabs_.erase(tabs_.begin() + index);
    content_stack_->remove(*content_stack_->get_child_by_name(std::to_string(index)));
    // Re-index remaining
    for (size_t i = 0; i < tabs_.size(); ++i) {
        tabs_[i]->set_property("name", std::to_string(i));
    }
    if (active_tab_ >= (int)tabs_.size()) active_tab_ = (int)tabs_.size() - 1;
    if (active_tab_ >= 0) {
        content_stack_->set_visible_child(*tabs_[active_tab_]);
        tabs_[active_tab_]->activate();
    }
    update_tab_bar();
    update_toolbar();
    update_status_bar();
}

void MainWindow::on_tab_switched(int) {
    if (auto* child = content_stack_->get_visible_child()) {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].get() == child) {
                active_tab_ = static_cast<int>(i);
                tabs_[i]->activate();
                break;
            }
        }
    }
    update_tab_bar();
    update_toolbar();
    update_status_bar();
}

void MainWindow::on_rename_window() {
    auto dialog = Gtk::make_managed<Gtk::Dialog>("Rename Window", *this, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Rename", Gtk::ResponseType::OK);
    dialog->set_default_response(Gtk::ResponseType::OK);

    auto* entry = Gtk::make_managed<Gtk::Entry>();
    if (core_ && core_->current_workspace()) {
        entry->set_text(core_->current_workspace()->name);
    }
    entry->set_hexpand(true);
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();

    dialog->signal_response().connect([this, dialog, entry](int response) {
        if (response == Gtk::ResponseType::OK && controller_) {
            controller_->rename_workspace(entry->get_text());
            update_header();
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::on_rename_tab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    auto dialog = Gtk::make_managed<Gtk::Dialog>("Rename Tab", *this, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Rename", Gtk::ResponseType::OK);
    dialog->set_default_response(Gtk::ResponseType::OK);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_text(tabs_[index]->title());
    entry->set_hexpand(true);
    dialog->get_content_area()->append(*entry);
    entry->grab_focus();
    dialog->signal_response().connect([this, index, dialog, entry](int response) {
        if (response == Gtk::ResponseType::OK) {
            const auto name = entry->get_text();
            if (!name.empty() && index >= 0 && index < (int)tabs_.size()) {
                tabs_[index]->set_title(name);
            }
            update_tab_bar();
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::on_split_terminal_horizontal() {
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
        tabs_[active_tab_]->kind() == TabKind::Terminal) {
        try {
            auto* t = static_cast<TerminalTabView*>(tabs_[active_tab_].get());
            t->split(remin::core::PaneTree::Kind::SplitHorizontal);
            update_status_bar();
        } catch (const std::exception& e) {
            // Log error but don't crash the GUI
            g_warning("Split horizontal failed: %s", e.what());
        }
    }
}

void MainWindow::on_split_terminal_vertical() {
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
        tabs_[active_tab_]->kind() == TabKind::Terminal) {
        try {
            auto* t = static_cast<TerminalTabView*>(tabs_[active_tab_].get());
            t->split(remin::core::PaneTree::Kind::SplitVertical);
            update_status_bar();
        } catch (const std::exception& e) {
            g_warning("Split vertical failed: %s", e.what());
        }
    }
}

void MainWindow::on_custom_split() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size() ||
        tabs_[active_tab_]->kind() != TabKind::Terminal) {
        return;
    }

    auto dialog = Gtk::make_managed<Gtk::Dialog>("Custom Split", *this, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Split", Gtk::ResponseType::OK);
    dialog->set_default_response(Gtk::ResponseType::OK);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    box->set_margin(16);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(8);
    grid->set_column_spacing(12);

    auto* rows_label = Gtk::make_managed<Gtk::Label>("Rows:");
    rows_label->set_halign(Gtk::Align::END);
    auto* rows_spin = Gtk::make_managed<Gtk::SpinButton>();
    rows_spin->set_range(1, 10);
    rows_spin->set_value(2);
    rows_spin->set_hexpand(true);

    auto* cols_label = Gtk::make_managed<Gtk::Label>("Columns:");
    cols_label->set_halign(Gtk::Align::END);
    auto* cols_spin = Gtk::make_managed<Gtk::SpinButton>();
    cols_spin->set_range(1, 10);
    cols_spin->set_value(2);
    cols_spin->set_hexpand(true);

    grid->attach(*rows_label, 0, 0, 1, 1);
    grid->attach(*rows_spin, 1, 0, 1, 1);
    grid->attach(*cols_label, 0, 1, 1, 1);
    grid->attach(*cols_spin, 1, 1, 1, 1);

    box->append(*grid);

    auto* info = Gtk::make_managed<Gtk::Label>(
        "Creates a grid of terminal panes (rows × columns). Each pane gets its own shell.");
    info->set_wrap(true);
    info->add_css_class("dim-label");
    info->set_margin_top(8);
    box->append(*info);

    dialog->get_content_area()->append(*box);
    dialog->signal_response().connect([this, dialog, rows_spin, cols_spin](int response) {
        if (response != Gtk::ResponseType::OK) return;

        int rows = static_cast<int>(rows_spin->get_value());
        int cols = static_cast<int>(cols_spin->get_value());
        if (rows < 1 || cols < 1 || rows > 10 || cols > 10) return;

        auto* t = static_cast<TerminalTabView*>(tabs_[active_tab_].get());
        if (!t) return;

        // Create the grid layout by splitting recursively
        // First split horizontally for rows, then each row vertically for cols
        // We'll use a simple approach: split horizontal for each row, then vertical within each
        try {
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    if (r == 0 && c == 0) continue; // Skip the first pane (already exists)
                    if (c == 0) {
                        // First column of each row: split horizontally from the previous row's first pane
                        t->split(remin::core::PaneTree::Kind::SplitHorizontal);
                    } else {
                        // Subsequent columns: split vertically from the previous column in the same row
                        t->split(remin::core::PaneTree::Kind::SplitVertical);
                    }
                }
            }
            update_status_bar();
        } catch (const std::exception& e) {
            g_warning("Custom split failed: %s", e.what());
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::on_close_terminal_pane() {
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
        tabs_[active_tab_]->kind() == TabKind::Terminal) {
        auto* t = static_cast<TerminalTabView*>(tabs_[active_tab_].get());
        if (t->close_focused_pane()) update_status_bar();
    }
}

void MainWindow::on_toggle_note_preview() {
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
        tabs_[active_tab_]->kind() == TabKind::Note) {
        static_cast<NoteTabView*>(tabs_[active_tab_].get())->toggle_preview();
    }
}

void MainWindow::update_tab_bar() {
    // Remove all current tab buttons, keeping the two "+" buttons.
    std::vector<Gtk::Widget*> stale;
    for (auto* c = tab_bar_->get_first_child(); c; c = c->get_next_sibling()) {
        if (c != new_terminal_btn_ && c != new_note_btn_) stale.push_back(c);
    }
    for (auto* w : stale) tab_bar_->remove(*w);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        // A tab is a horizontal pair of two sibling buttons [switch][close].
        // They are NOT nested inside one another so both stay clickable.
        auto* tab = Gtk::make_managed<Gtk::Button>();
        tab->add_css_class("remin-tab");

        auto* inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* img = Gtk::make_managed<Gtk::Image>();
        img->set_from_icon_name(tab_kind_icon(tabs_[i]->kind()));
        img->set_pixel_size(16);
        inner->append(*img);
        auto* lbl = Gtk::make_managed<Gtk::Label>(tabs_[i]->title());
        lbl->set_ellipsize(Pango::EllipsizeMode::END);
        inner->append(*lbl);
        tab->set_child(*inner);

        tab->signal_clicked().connect([this, i]() {
            content_stack_->set_visible_child(*tabs_[i]);
        });
        if (static_cast<int>(i) == active_tab_) tab->add_css_class("active");

        // Right-click on the label area renames the tab.
        auto g = Gtk::GestureClick::create();
        g->set_button(3);
        g->signal_pressed().connect([this, i](int, double, double) {
            content_stack_->set_visible_child(*tabs_[i]);
            on_rename_tab(static_cast<int>(i));
        });
        tab->add_controller(g);

        auto* close = Gtk::make_managed<Gtk::Button>("✕");
        close->add_css_class("remin-tab-close");
        close->set_tooltip_text("Close tab");
        close->signal_clicked().connect([this, i]() { close_tab(static_cast<int>(i)); });

        auto* group = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        group->add_css_class("remin-tab-group");
        group->append(*tab);
        group->append(*close);
        tab_bar_->append(*group);
    }
}

void MainWindow::update_status_bar() {
    if (!status_label_) return;
    std::string text;
    if (core_ && core_->current_workspace()) {
        text = "Workspace: " + core_->current_workspace()->name;
    }
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size()) {
        auto* tab = tabs_[active_tab_].get();
        text += "  |  ";
        text += (tab->kind() == TabKind::Terminal ? "Terminal" : "Note");
        if (tab->kind() == TabKind::Terminal) {
            // Could show pane count
        }
    }
    status_label_->set_text(text);
}

void MainWindow::update_header() {
    if (header_label_ && core_ && core_->current_workspace()) {
        header_label_->set_text("Remin \u2014 " + core_->current_workspace()->name);
    }
}

void MainWindow::show_find_bar(bool replace) {
    find_bar_->set_visible(true);
    find_entry_->grab_focus();
    find_entry_->set_text("");
    replace_entry_->set_text("");
    replace_entry_->set_visible(replace);
    replace_icon_->set_visible(replace);
    replace_btn_->set_visible(replace);
    replace_all_btn_->set_visible(replace);
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size()) {
        auto* tab = tabs_[active_tab_].get();
        if (tab->kind() == TabKind::Note) {
            auto* n = static_cast<NoteTabView*>(tab);
            n->show_find_replace(replace);
        }
    }
}

void MainWindow::hide_find_bar() {
    find_bar_->set_visible(false);
    replace_entry_->set_visible(false);
    replace_icon_->set_visible(false);
    replace_btn_->set_visible(false);
    replace_all_btn_->set_visible(false);
    // Also hide note editor's find bar if visible
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size()) {
        auto* tab = tabs_[active_tab_].get();
        if (tab->kind() == TabKind::Note) {
            auto* n = static_cast<NoteTabView*>(tab);
            if (n->editor()) {
                n->editor()->show_find(false);
            }
        }
    }
}

bool MainWindow::on_find_key_pressed(guint keyval, guint, Gdk::ModifierType mods) {
    // ESC closes find bar
    if (keyval == GDK_KEY_Escape) {
        if (find_bar_->get_visible()) {
            hide_find_bar();
            return true;
        }
    }

    bool ctrl = (mods & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType(0);
    bool shift = (mods & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType(0);

    if (ctrl && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        show_find_bar(false);
        return true;
    }
    if (ctrl && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        show_find_bar(true);
        return true;
    }
    if (ctrl && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
        if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
            tabs_[active_tab_]->kind() == TabKind::Note) {
            static_cast<NoteTabView*>(tabs_[active_tab_].get())->save_now();
        }
        return true;
    }
    if (ctrl && (keyval == GDK_KEY_t || keyval == GDK_KEY_T)) {
        new_terminal_tab();
        return true;
    }
    if (ctrl && shift && (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        new_note_tab();
        return true;
    }
    if (ctrl && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        close_tab(active_tab_);
        return true;
    }
    // Split shortcuts: Shift+H = horizontal, Shift+V = vertical
    if (shift && !ctrl && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        on_split_terminal_horizontal();
        return true;
    }
    if (shift && !ctrl && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        on_split_terminal_vertical();
        return true;
    }
    return false;
}

void MainWindow::on_find_next() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() == TabKind::Terminal) {
        auto* t = static_cast<TerminalTabView*>(tab);
        if (auto* p = t->focused_pane()) {
            p->set_search_text(find_entry_->get_text());
            p->search_next();
        }
    } else {
        static_cast<NoteTabView*>(tab)->focus_search();
    }
}

void MainWindow::on_find_prev() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() == TabKind::Terminal) {
        auto* t = static_cast<TerminalTabView*>(tab);
        if (auto* p = t->focused_pane()) {
            p->set_search_text(find_entry_->get_text());
            p->search_previous();
        }
    } else {
        static_cast<NoteTabView*>(tab)->focus_search();
    }
}

void MainWindow::on_replace() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() == TabKind::Note) {
        auto* n = static_cast<NoteTabView*>(tab);
        if (n->editor()) {
            n->editor()->do_replace();
        }
    }
}

void MainWindow::on_replace_all() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() == TabKind::Note) {
        auto* n = static_cast<NoteTabView*>(tab);
        if (n->editor()) {
            n->editor()->do_replace_all();
        }
    }
}

void MainWindow::show_autosave_badge(bool success) {
    autosave_badge_->set_text(success ? "saved \u2713" : "save failed");
    autosave_badge_->remove_css_class(success ? "autosave-fail" : "autosave-ok");
    autosave_badge_->add_css_class(success ? "autosave-ok" : "autosave-fail");
    autosave_badge_->set_visible(true);

    if (autosave_badge_hide_.connected()) autosave_badge_hide_.disconnect();
    autosave_badge_hide_ = Glib::signal_timeout().connect(
        [this]() { autosave_badge_->set_visible(false); return false; },
        2000);
}

void MainWindow::on_note_save_as() {
    if (active_tab_ >= 0 && active_tab_ < (int)tabs_.size() &&
        tabs_[active_tab_]->kind() == TabKind::Note) {
        static_cast<NoteTabView*>(tabs_[active_tab_].get())->save_as();
    }
}

void MainWindow::toggle_history_sidebar() {
    if (!main_paned_) return;
    sidebar_visible_ = !sidebar_visible_;
    if (sidebar_visible_) {
        main_paned_->set_position(220);
        // Refresh if showing directory
        if (sidebar_stack_ && sidebar_stack_->get_visible_child_name() == "directory") {
            refresh_directory();
        }
    } else {
        main_paned_->set_position(0);
    }
}

void MainWindow::on_settings() {
    auto dialog = Gtk::make_managed<SettingsDialog>(*this, theme_, controller_);
    dialog->set_transient_for(*this);
    dialog->present();
}

void MainWindow::open_note_from_path(const std::filesystem::path& path) {
    // Create a new note tab and load the file
    auto noteId = controller_->new_note();
    if (noteId.empty()) return;

    auto* view = new NoteTabView(controller_, noteId);
    auto idx = static_cast<int>(tabs_.size());
    tabs_.push_back(std::unique_ptr<TabView>(view));
    note_tabs_.push_back(view);

    content_stack_->add(*view, std::to_string(idx));
    update_tab_bar();
    update_toolbar();
    content_stack_->set_visible_child(*view);
    active_tab_ = idx;
    view->activate();
    update_status_bar();

    // Load the file into the editor
    view->load_file(path);
}

void MainWindow::refresh_theme() {
    // The ThemeManager applies its provider for the whole display; nothing more
    // is required than re-tagging the window (idempotent).
    if (theme_) ThemeManager::tag_window(*this);
}

void MainWindow::on_terminal_color_profile() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size() ||
        tabs_[active_tab_]->kind() != TabKind::Terminal) {
        return;
    }
    auto* t = static_cast<TerminalTabView*>(tabs_[active_tab_].get());

    auto dialog = Gtk::make_managed<Gtk::Dialog>("Terminal Color Profile", *this, true);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Apply", Gtk::ResponseType::OK);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(12);

    auto* fg_btn = Gtk::make_managed<Gtk::ColorButton>();
    auto* bg_btn = Gtk::make_managed<Gtk::ColorButton>();
    auto* fg_lbl = Gtk::make_managed<Gtk::Label>("Foreground:");
    auto* bg_lbl = Gtk::make_managed<Gtk::Label>("Background:");

    auto* row_fg = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row_fg->append(*fg_lbl);
    row_fg->append(*fg_btn);
    auto* row_bg = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row_bg->append(*bg_lbl);
    row_bg->append(*bg_btn);

    box->append(*row_fg);
    box->append(*row_bg);
    dialog->get_content_area()->append(*box);
    dialog->present();

    dialog->signal_response().connect([this, t, dialog, fg_btn, bg_btn](int response) {
        if (response == Gtk::ResponseType::OK) {
            auto* pane = t->focused_pane();
            if (pane) {
                pane->set_colors(fg_btn->get_rgba(), bg_btn->get_rgba());
            }
        }
        dialog->close();
    });
}

} // namespace remin::gui