#include "gui/window/main_window.hpp"
#include "gui/window/settings_dialog.hpp"
#include <adwaita.h>
#include <terminal/shell/shell.hpp>

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

    // Register bundled icon resource path (icons loaded via GResource in Application)
    try {
        auto icon_theme = Gtk::IconTheme::get_for_display(get_display());
        icon_theme->add_resource_path("/icons/hicolor/scalable");
        icon_theme->add_search_path(std::string(REMIN_RESOURCE_DIR) + "/icons");
    } catch (const Glib::Error&) {}
    set_icon_name("remin");

    // Add remin-window CSS class for CSS specificity
    add_css_class("remin-window");

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
    root->append(*tab_box_);
    root->append(*main_paned_);
    root->append(*status_bar_);

    // Global key controller for accelerators (capture phase to intercept before VTE)
    key_ctrl_ = Gtk::EventControllerKey::create();
    key_ctrl_->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key_ctrl_->signal_key_pressed().connect(
        sigc::slot<bool(unsigned int, unsigned int, Gdk::ModifierType)>(
            [this](unsigned int keyval, unsigned int, Gdk::ModifierType mods) -> bool {
                return on_find_key_pressed(keyval, 0, mods);
            }),
        false);
    add_controller(key_ctrl_);

    // The find/replace bar stays open until closed explicitly (X button or
    // Esc). Clicking elsewhere (including the notebook editor) does NOT dismiss
    // it, so a search survives focus switches.

    // Click outside terminal/note area to clear focus (allows global shortcuts like Ctrl+F/H to work)
    auto focus_click_ctrl = Gtk::GestureClick::create();
    focus_click_ctrl->set_button(0);
    focus_click_ctrl->signal_released().connect([this](int, double x, double y) {
        // Find the widget at the click coordinates
        auto* root = get_child();
        if (!root) return;
        auto* target = root->pick(x, y, Gtk::PickFlags::DEFAULT);
        if (!target) return;

        // Ignore clicks inside find_bar_ (buttons, entries, etc.)
        for (auto* w = target; w; w = w->get_parent()) {
            if (w == find_bar_) return;
        }

        // Check if focus is in a terminal pane or note editor
        auto* focus_widget = get_focus();
        if (!focus_widget) return;

        bool focus_in_editable = false;
        for (auto* w = focus_widget; w; w = w->get_parent()) {
            if (dynamic_cast<TerminalTabView*>(w)) { focus_in_editable = true; break; }
            if (dynamic_cast<NoteTabView*>(w)) { focus_in_editable = true; break; }
        }
        if (!focus_in_editable) return;

        // Check if click is outside the focused editable widget
        auto allocation = focus_widget->get_allocation();
        if (x < allocation.get_x() || x > allocation.get_x() + allocation.get_width() ||
            y < allocation.get_y() || y > allocation.get_y() + allocation.get_height()) {
            // Click outside - clear focus by focusing the window itself
            grab_focus();
        }
    });
    add_controller(focus_click_ctrl);

    // Restore workspace state (windows/tabs/panes) from core, if any.
    restore_workspace();

    // If no tabs were restored, open a default terminal tab
    if (tabs_.empty()) {
        new_terminal_tab();
    }

    // Apply startup sidebar visibility from the persisted setting. Default is
    // closed; the toggle icon must match the actual (open/closed) state.
    apply_initial_sidebar_state();

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

// NOTE: Logo deliberately disabled for now — the current SVG didn't fit the
// header visually. Commented out (not removed) so a future logo can be
// re-enabled easily by uncommenting the block below.
//
// const std::string logo_path = std::string(REMIN_RESOURCE_DIR) + "/logo.svg";
//     try {
//         auto tex = Gdk::Texture::create_from_filename(logo_path);
//         logo_image_ = Gtk::make_managed<Gtk::Picture>();
//         logo_image_->set_content_fit(Gtk::ContentFit::CONTAIN);
//         logo_image_->set_paintable(tex);
//         // Wide logo; size the box to the text aspect so it renders full-width
//         // without inflating the 46px header (GtkImage+set_pixel_size would
//         // force a square and squash the wide texture).
//         logo_image_->set_size_request(140, 30);
//     } catch (const Glib::Error&) {}

    header_label_ = Gtk::make_managed<Gtk::Label>("Remin");
    header_label_->add_css_class("remin-header-label");
    header_box_->append(*header_label_);

    header_->set_title_widget(*header_box_);

    // Sidebar toggle button (top-right), label reflects current state.
    // Frame-less so only the icon shows (no button chrome/background/border).
    sidebar_toggle_btn_ = Gtk::make_managed<Gtk::Button>();
    sidebar_toggle_btn_->set_has_frame(false);
    sidebar_toggle_btn_->add_css_class("remin-toolbar-btn");
    sidebar_toggle_btn_->add_css_class("remin-sidebar-toggle");
    sidebar_toggle_btn_->set_icon_name("sidebar-show-symbolic");
    sidebar_toggle_btn_->set_tooltip_text("Toggle Sidebar (Ctrl+P)");
    sidebar_toggle_btn_->signal_clicked().connect(
        sigc::mem_fun(*this, &MainWindow::toggle_history_sidebar));
    header_->pack_end(*sidebar_toggle_btn_);
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
    terminal_menu->append("Split Horizontally (Alt+H)", "win.split_h");
    terminal_menu->append("Split Vertically (Alt+V)", "win.split_v");
    terminal_menu->append("Custom Split…", "win.custom_split");
    terminal_menu->append("Close Pane (Alt+K)", "win.close_pane");
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
    history_menu->append("Show/Hide Panel", "win.toggle_history");
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
        AdwStyleManager* sm = adw_style_manager_get_default();
        AdwColorScheme current = adw_style_manager_get_color_scheme(sm);
        adw_style_manager_set_color_scheme(sm, current == ADW_COLOR_SCHEME_PREFER_DARK ? ADW_COLOR_SCHEME_FORCE_LIGHT : ADW_COLOR_SCHEME_PREFER_DARK);
    });
    actions->add_action("reload", [this]() {
        AdwStyleManager* sm = adw_style_manager_get_default();
        adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
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
    toolbar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
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
    auto* img = Gtk::make_managed<Gtk::Image>();
    img->set_from_icon_name(icon);
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
        // Use Unicode box-drawing characters for split icons
        auto* split_h = Gtk::make_managed<Gtk::Button>("⬒");
        split_h->add_css_class("remin-tool-btn");
        split_h->set_tooltip_text("Split Horizontal (Alt+H)");
        split_h->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_split_terminal_horizontal));
        toolbar_->append(*split_h);

        auto* split_v = Gtk::make_managed<Gtk::Button>("◧");
        split_v->add_css_class("remin-tool-btn");
        split_v->set_tooltip_text("Split Vertical (Alt+V)");
        split_v->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_split_terminal_vertical));
        toolbar_->append(*split_v);
        toolbar_->append(*make_tool_btn(
            "window-close-symbolic", "Close", "Close Pane (Alt+K)",
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
            "document-open-symbolic", "Open", "Open File… (Ctrl+O)",
            [this]() {
                // Open file dialog for note editor
                auto* win = dynamic_cast<Gtk::Window*>(get_root());
                if (!win) return;
                auto dialog = Gtk::make_managed<Gtk::FileChooserDialog>(
                    *win, "Open File", Gtk::FileChooser::Action::OPEN);
                dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
                dialog->add_button("Open", Gtk::ResponseType::OK);
                dialog->set_modal(true);
                dialog->signal_response().connect([this, dialog](int response) {
                    if (response == Gtk::ResponseType::OK) {
                        const auto path = dialog->get_file()->get_path();
                        if (!path.empty()) open_note_from_path(path);
                    }
                    dialog->close();
                });
                dialog->present();
            }));
        toolbar_->append(*make_tool_btn(
            "view-paged-symbolic", "Preview", "Toggle Preview",
            sigc::mem_fun(*this, &MainWindow::on_toggle_note_preview)));
        toolbar_->append(*make_tool_btn(
            "edit-find-replace-symbolic", "Replace", "Find / Replace (Ctrl+H)",
            [this]() { show_find_bar(true); }));
    }
}

void MainWindow::setup_tab_bar() {
    // Horizontally scrollable tab row: with many tabs the row scrolls sideways
    // (VS Code style) instead of stretching tabs across the window.
    //
    // The horizontal scrollbar lives in its OWN row below the labels (a
    // dedicated Gtk::Scrollbar), so overflow never overlaps or eats the tab
    // label strip. It is shown only when the tab row actually overflows.
    // The + buttons are FIXED at the right, NOT part of the scrolling tab list.
    tab_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    tab_box_->add_css_class("remin-tab-box");

    // Horizontal container: tabs scroller (flex) + fixed + buttons (right)
    auto* tab_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    tab_row->add_css_class("remin-tab-row");

    tab_scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    // EXTERNAL horizontal policy: the window NEVER grows to fit the tabs —
    // overflow is reported through the hadjustment and rendered by the
    // dedicated scrollbar row below the labels. No internal scrollbar is drawn.
    tab_scroller_->set_policy(Gtk::PolicyType::EXTERNAL, Gtk::PolicyType::NEVER);
    tab_scroller_->set_vexpand(false);
    tab_scroller_->set_hexpand(true);
    tab_scroller_->set_halign(Gtk::Align::FILL);

    tab_bar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    tab_bar_->add_css_class("remin-tab-bar");
    tab_bar_->set_margin_top(4);
    tab_bar_->set_margin_bottom(4);
    tab_bar_->set_margin_start(8);  // 8px gap left aligned with sidebar
    tab_bar_->set_margin_end(8);
    tab_bar_->set_hexpand(false); // Don't let tab bar expand beyond scroller
    tab_scroller_->set_child(*tab_bar_);

    // Dedicated horizontal scrollbar, linked to the scroller's adjustment.
    auto hadj = tab_scroller_->get_hadjustment();
    tab_scrollbar_ = Gtk::make_managed<Gtk::Scrollbar>(hadj);
    tab_scrollbar_->set_orientation(Gtk::Orientation::HORIZONTAL);
    tab_scrollbar_->add_css_class("remin-tab-scrollbar");
    tab_scrollbar_->set_visible(false);

    // Fixed + buttons at the right (outside scroller)
    auto* tab_actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    tab_actions->add_css_class("remin-tab-actions");
    tab_actions->set_margin_end(8);
    tab_actions->set_valign(Gtk::Align::CENTER);

    new_terminal_btn_ = Gtk::make_managed<Gtk::Button>();
    new_terminal_btn_->add_css_class("remin-tab-btn");
    new_terminal_btn_->set_icon_name("list-add-symbolic");
    new_terminal_btn_->set_tooltip_text("New Terminal Tab (Ctrl+T)");
    new_terminal_btn_->set_valign(Gtk::Align::CENTER);
    new_terminal_btn_->set_halign(Gtk::Align::CENTER);
    new_terminal_btn_->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::new_terminal_tab));
    tab_actions->append(*new_terminal_btn_);

    new_note_btn_ = Gtk::make_managed<Gtk::Button>();
    new_note_btn_->add_css_class("remin-tab-btn");
    new_note_btn_->set_icon_name("document-new-symbolic");
    new_note_btn_->set_tooltip_text("New Note Tab (Ctrl+Shift+N)");
    new_note_btn_->set_valign(Gtk::Align::CENTER);
    new_note_btn_->set_halign(Gtk::Align::CENTER);
    new_note_btn_->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::new_note_tab));
    tab_actions->append(*new_note_btn_);

    tab_row->append(*tab_scroller_);
    tab_row->append(*tab_actions);

    tab_box_->append(*tab_row);
    tab_box_->append(*tab_scrollbar_);

    // Show the scrollbar only when the tab row overflows. Fires on range
    // changes (event-driven, no polling).
    auto update_overflow = [this]() {
        auto hadj = tab_scroller_->get_hadjustment();
        bool overflow = hadj && hadj->get_upper() > hadj->get_page_size() + 0.5;
        if (tab_scrollbar_ && tab_scrollbar_->get_visible() != overflow) {
            tab_scrollbar_->set_visible(overflow);
        }
    };
    tab_scroller_->get_hadjustment()->signal_changed().connect(update_overflow);
    tab_scroller_->get_vadjustment()->signal_changed().connect(update_overflow);
    update_tab_overflow_fn = std::move(update_overflow);
}

void MainWindow::setup_content_stack() {
    content_stack_ = Gtk::make_managed<Gtk::Stack>();
    content_stack_->set_hexpand(true);
    content_stack_->set_vexpand(true);
    content_stack_->set_transition_type(Gtk::StackTransitionType::SLIDE_LEFT_RIGHT);
    // Right margin for workspace content (match sidebar left margin)
    content_stack_->set_margin_end(8);

    // Main paned: [sidebar | content_stack]
    main_paned_ = Gtk::make_managed<Gtk::Paned>();
    main_paned_->set_orientation(Gtk::Orientation::HORIZONTAL);
    main_paned_->set_hexpand(true);
    main_paned_->set_vexpand(true);
    main_paned_->set_wide_handle(false);
    main_paned_->set_start_child(*sidebar_root_);
    main_paned_->set_end_child(*content_stack_);
    // Default closed; auto-open is applied in the constructor via
    // apply_initial_sidebar_state() if the user opted in.
    main_paned_->set_position(0);
    sidebar_visible_ = false;
}

void MainWindow::restore_workspace() {
    if (!core_ || !core_->current_workspace()) return;
    const auto* ws = core_->current_workspace();
    if (ws->windows.empty()) return;

    // For V1, we only handle the first window (this MainWindow).
    const auto& win = ws->windows.front();
    if (win.tabs.empty()) return;

    // Restore each tab from the core's workspace state
    for (const auto& tab : win.tabs) {
        // Check if this is a terminal tab (has a Pane pane_tree with a valid pane)
        bool is_terminal = false;
        remin::core::PaneId root_pane;
        if (tab.pane_tree.kind() == remin::core::PaneTree::Kind::Pane &&
            tab.pane_tree.pane().has_value()) {
            is_terminal = true;
            root_pane = tab.pane_tree.pane()->id;
        }

        if (is_terminal) {
            // Create TerminalTabView directly with the existing core IDs
            // bypassing controller_->new_terminal_tab() which would create new core objects
            auto* view = new TerminalTabView(controller_, this, win.id, tab.id, root_pane);
            view->set_close_tab_request_callback([this, view]() {
                // Find the current index of this view
                for (size_t i = 0; i < tabs_.size(); ++i) {
                    if (tabs_[i].get() == view) {
                        close_tab(static_cast<int>(i));
                        break;
                    }
                }
            });
            tabs_.push_back(std::unique_ptr<TabView>(view));
            term_tabs_.push_back(view);

            content_stack_->add(*view, std::to_string(tabs_.size() - 1));
            // Don't activate yet - we'll activate the focused one after all tabs are created
        }
        // TODO: Handle note tabs - they need a different approach
    }

    // Activate the focused tab (or first tab if none focused)
    if (!tabs_.empty()) {
        auto focus_idx = 0;
        if (win.focus_tab_id.has_value()) {
            for (size_t i = 0; i < tabs_.size(); ++i) {
                if (i < win.tabs.size() && win.tabs[i].id == win.focus_tab_id) {
                    focus_idx = static_cast<int>(i);
                    break;
                }
            }
        }
        active_tab_ = focus_idx;
        content_stack_->set_visible_child(*tabs_[focus_idx]);
        tabs_[focus_idx]->activate();
    }

    update_tab_bar();
    update_toolbar();
    update_status_bar();
}

void MainWindow::setup_sidebar() {
    // Shared sidebar mode switcher — History | Files, always both visible so
    // switching modes never "hides" the other tab.
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
    sidebar_mode_tabs_[0] = history_tab;
    sidebar_mode_tabs_[1] = directory_tab;

    // Sidebar stack containing the two pages.
    sidebar_stack_ = Gtk::make_managed<Gtk::Stack>();
    sidebar_stack_->set_hexpand(false);
    sidebar_stack_->set_vexpand(true);

    // History page.
    history_scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    history_scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    history_scroller_->set_vexpand(true);
    history_list_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    history_scroller_->set_child(*history_list_);
    sidebar_stack_->add(*history_scroller_, "history", "History");

    // Directory page — delegated to a focused DirectoryTreePanel.
    directory_panel_ = Gtk::make_managed<DirectoryTreePanel>(
        [this](const std::filesystem::path& path) { open_note_from_path(path); });
    sidebar_stack_->add(*directory_panel_, "directory", "Files");

    // Top-level sidebar vertical box: switcher always on top, stack below.
    auto* sidebar_root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    sidebar_root->add_css_class("remin-sidebar");
    sidebar_root->append(*tab_bar);
    sidebar_root->append(*sidebar_stack_);
    sidebar_root_ = sidebar_root;

    const char* home = std::getenv("HOME");
    if (directory_panel_) directory_panel_->set_root(home ? home : "/");

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
        if (sidebar_mode_tabs_[0]) sidebar_mode_tabs_[0]->add_css_class("active");
        if (sidebar_mode_tabs_[1]) sidebar_mode_tabs_[1]->remove_css_class("active");
        // Refresh history list when switching to history mode
        update_history_sidebar();
    } else if (mode == "directory") {
        sidebar_stack_->set_visible_child("directory");
        if (sidebar_mode_tabs_[1]) sidebar_mode_tabs_[1]->add_css_class("active");
        if (sidebar_mode_tabs_[0]) sidebar_mode_tabs_[0]->remove_css_class("active");
        if (directory_panel_) {
            if (first_directory_show_) {
                directory_panel_->refresh_to_top();
                first_directory_show_ = false;
            } else {
                directory_panel_->refresh();
            }
        }
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

void MainWindow::setup_status_bar() {
    status_bar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    status_bar_->add_css_class("remin-status-bar");
    status_bar_->set_margin_start(8);
    status_bar_->set_margin_end(8);
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

    auto* find_icon = Gtk::make_managed<Gtk::Image>();
    find_icon->set_from_icon_name("edit-find-symbolic");
    find_icon->set_pixel_size(16);
    find_entry_ = Gtk::make_managed<Gtk::Entry>();
    find_entry_->add_css_class("remin-find-entry");
    find_entry_->set_hexpand(true);
    find_entry_->set_placeholder_text("Search…");
    find_entry_->signal_activate().connect([this]() { on_find_next(); });
    find_entry_->signal_changed().connect([this]() { sync_find_text(); });

    auto* replace_icon = Gtk::make_managed<Gtk::Image>();
    replace_icon->set_from_icon_name("edit-find-replace-symbolic");
    replace_icon->set_pixel_size(16);
    replace_entry_ = Gtk::make_managed<Gtk::Entry>();
    replace_entry_->add_css_class("remin-find-entry");
    replace_entry_->set_hexpand(true);
    replace_entry_->set_placeholder_text("Replace with…");
    replace_entry_->set_visible(false);
    replace_icon->set_visible(false);

    find_prev_ = Gtk::make_managed<Gtk::Button>();
    find_prev_->add_css_class("remin-find-btn");
    auto* prev_img = Gtk::make_managed<Gtk::Image>();
    prev_img->set_from_icon_name("go-previous-symbolic");
    find_prev_->set_child(*prev_img);
    find_prev_->set_tooltip_text("Previous (Shift+Enter)");
    find_prev_->signal_clicked().connect([this]() { on_find_prev(); });

    find_next_ = Gtk::make_managed<Gtk::Button>();
    find_next_->add_css_class("remin-find-btn");
    auto* next_img = Gtk::make_managed<Gtk::Image>();
    next_img->set_from_icon_name("go-next-symbolic");
    find_next_->set_child(*next_img);
    find_next_->set_tooltip_text("Next (Enter)");
    find_next_->signal_clicked().connect([this]() { on_find_next(); });

    replace_btn_ = Gtk::make_managed<Gtk::Button>("Replace");
    replace_btn_->add_css_class("remin-find-btn");
    replace_btn_->set_visible(false);
    replace_btn_->signal_clicked().connect([this]() { on_replace(); });

    replace_all_btn_ = Gtk::make_managed<Gtk::Button>("Replace All");
    replace_all_btn_->add_css_class("remin-find-btn");
    replace_all_btn_->set_visible(false);
    replace_all_btn_->signal_clicked().connect([this]() { on_replace_all(); });

    find_close_ = Gtk::make_managed<Gtk::Button>();
    find_close_->add_css_class("remin-find-btn");
    auto* close_img = Gtk::make_managed<Gtk::Image>();
    close_img->set_from_icon_name("window-close-symbolic");
    find_close_->set_child(*close_img);
    find_close_->set_tooltip_text("Close (Esc)");
    find_close_->signal_clicked().connect([this]() { hide_find_bar(); });

    find_bar_->append(*find_icon);
    find_bar_->append(*find_entry_);
    find_bar_->append(*replace_icon);
    find_bar_->append(*replace_entry_);

    // Match count label
    find_match_label_ = Gtk::make_managed<Gtk::Label>("");
    find_match_label_->add_css_class("remin-find-match-label");
    find_match_label_->set_visible(false);
    find_match_label_->set_margin_start(8);
    find_bar_->append(*find_match_label_);

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

    auto* view = new TerminalTabView(controller_, this, tab_info.window, tab_info.tab, tab_info.root_pane);
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
    view->set_close_tab_request_callback([this, view]() {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].get() == view) {
                close_tab(static_cast<int>(i));
                break;
            }
        }
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
    view->set_save_state_callback([this]() {
        update_tab_bar();
    });
    view->set_file_saved_callback([this](const std::filesystem::path& saved) {
        if (directory_panel_) directory_panel_->on_note_saved(saved);
    });
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

    // Unsaved-note guard: before destroying a note tab with unsaved edits,
    // honour the user's close preference (Keep = save & close / Skip = close
    // without saving / Ask = prompt each time). The prompt is non-blocking, so
    // the actual removal happens in finish_close_tab afterwards.
    if (tabs_[index]->kind() == TabKind::Note) {
        auto* note = static_cast<NoteTabView*>(tabs_[index].get());
        if (note->is_modified()) {
            auto behavior = controller_
                                ? controller_->unsaved_close_behavior()
                                : SessionController::UnsavedClose::Ask;
            if (behavior == SessionController::UnsavedClose::Keep) {
                close_keep_note(note, index);
                return;  // aborted if the user cancels saving
            }
            if (behavior == SessionController::UnsavedClose::Ask) {
                prompt_close_note(note, index);
                return;
            }
            // Skip: fall through and close without saving.
        }
    }

    finish_close_tab(index);
}

void MainWindow::close_keep_note(NoteTabView* note, int index) {
    if (note->has_path()) {
        // Bound to a file: save synchronously, then close.
        note->save_now();
        finish_close_tab(index);
        return;
    }
    // Temp note without a file path: always show the Save dialog. The tab is
    // closed only after a successful save; cancelling aborts the close, keeping
    // the user's work available in the editor.
    //
    // The index is NOT baked in here: the Save dialog is async and tabs may
    // have shifted by the time the user confirms, so the real index is resolved
    // from the note pointer at completion time.
    note->save_as([this, note]() {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].get() == note) {
                finish_close_tab(static_cast<int>(i));
                return;
            }
        }
    });
}

void MainWindow::prompt_close_note(NoteTabView* note, int index) {
    auto dialog = Gtk::make_managed<Gtk::MessageDialog>(
        *this, "Unsaved changes",
        false, Gtk::MessageType::QUESTION, Gtk::ButtonsType::NONE, false);
    dialog->set_secondary_text(
        "The note \"" + note->title() + "\" has unsaved changes.\n"
        "Keep saves the note and then closes the tab; Skip closes the tab "
        "without saving. If the note has no file path yet, Keep opens the Save "
        "dialog first (cancelling that also cancels closing the tab).");
    auto keep_btn = dialog->add_button("_Keep", Gtk::ResponseType::OK);
    auto skip_btn = dialog->add_button("_Skip", Gtk::ResponseType::CANCEL);
    (void)keep_btn; (void)skip_btn;
    dialog->set_default_response(Gtk::ResponseType::CANCEL);
    dialog->set_modal(true);
    dialog->signal_response().connect([this, note, index, dialog](int response) {
        if (response == Gtk::ResponseType::OK) {
            close_keep_note(note, index);
        } else {
            finish_close_tab(index);  // Skip (ESC/cancel) = close w/o saving
        }
        dialog->close();
    });
    dialog->present();
}

void MainWindow::finish_close_tab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;  // stale-index guard

    auto kind = tabs_[index]->kind();
    // Detach the widget from the stack BEFORE destroying it (tabs_ holds ownership).
    Gtk::Widget* child = content_stack_->get_child_by_name(std::to_string(index));
    if (child) content_stack_->remove(*child);

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

    // Fix the active index BEFORE removal so all later numbering is consistent:
    // tabs after the closed one shift down. If we closed the active tab itself,
    // -1 means "recompute below" (fall back to the last remaining tab).
    if (index < active_tab_) {
        --active_tab_;
    } else if (index == active_tab_) {
        active_tab_ = -1;
    }

    tabs_.erase(tabs_.begin() + index);
    // Rebuild the stack children with fresh index names so they stay in sync
    // with tabs_ indices. Removing each child just detaches it (tabs_ owns the
    // TabView), then we re-add at their new positions.
    while (auto* child = content_stack_->get_first_child()) {
        content_stack_->remove(*child);
    }
    for (size_t i = 0; i < tabs_.size(); ++i) {
        content_stack_->add(*tabs_[i].get(), std::to_string(i));
    }
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) {
        active_tab_ = (int)tabs_.size() - 1;
    }
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
    if (update_tab_overflow_fn) update_tab_overflow_fn();
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
    if (!tab_bar_) return;

    // Any shrink (a tab was closed/reordered) invalidates captured widget →
    // tab-identity bindings, so rebuild the row cleanly. Pure appends keep the
    // existing widgets and just add the new ones (fast path).
    if (tabs_.size() < tab_widgets_.size()) {
        for (auto* w : tab_widgets_) tab_bar_->remove(*w);
        tab_widgets_.clear();
    }
    while (tab_widgets_.size() > tabs_.size()) {
        auto* w = tab_widgets_.back();
        tab_bar_->remove(*w);
        tab_widgets_.pop_back();
    }

    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (i >= tab_widgets_.size()) {
            auto* tab = build_tab_widget(i);
            tab_widgets_.push_back(tab);
            tab_bar_->append(*tab);
        }
        refresh_tab_widget(tab_widgets_[i], i);
    }
}

Gtk::Box* MainWindow::build_tab_widget(size_t index) {
    auto* view = tabs_[index].get(); // stable identity for the lifetime of the tab

    // A tab is ONE container: [icon + label ........ × ], so the close button
    // sits INSIDE the tab label area (merged), not detached beside it. The
    // label area fills the tab and pushes × to the far right.
    auto* tab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    tab->add_css_class("remin-tab");
    tab->set_size_request(96, -1);

    // Clickable label region (icon + text) — clicking it switches the tab.
    auto* click_area = Gtk::make_managed<Gtk::Button>();
    click_area->add_css_class("remin-tab-label");
    click_area->set_hexpand(true);

    auto* inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* img = Gtk::make_managed<Gtk::Image>();
    img->set_from_icon_name(tab_kind_icon(view->kind()));
    img->set_pixel_size(14);
    inner->append(*img);

    auto* lbl = Gtk::make_managed<Gtk::Label>();
    // NO ellipsis — labels stay full width. Tab bar scrolls if needed.
    lbl->set_hexpand(true);
    lbl->set_halign(Gtk::Align::START);
    inner->append(*lbl);
    click_area->set_child(*inner);

    // Resolve the tab's current index from its stable TTabView* at click time
    // (indices shift as tabs open/close; the pointer never does).
    auto find_tab_index = [this, view]() -> int {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].get() == view) return static_cast<int>(i);
        }
        return -1;
    };

    click_area->signal_clicked().connect([this, find_tab_index]() {
        const int idx = find_tab_index();
        if (idx >= 0) content_stack_->set_visible_child(*tabs_[idx]);
    });

    // Right-click on the label area renames the tab.
    auto g = Gtk::GestureClick::create();
    g->set_button(3);
    g->signal_pressed().connect([this, find_tab_index](int, double, double) {
        const int idx = find_tab_index();
        if (idx >= 0) {
            content_stack_->set_visible_child(*tabs_[idx]);
            on_rename_tab(idx);
        }
    });
    click_area->add_controller(g);

    // Close button INSIDE the tab, at the far right (round / transparent).
    auto* close = Gtk::make_managed<Gtk::Button>();
    close->add_css_class("remin-tab-close");
    close->set_has_frame(false);
    close->set_icon_name("window-close-symbolic");
    close->set_tooltip_text("Close tab");
    close->set_valign(Gtk::Align::CENTER);
    close->set_halign(Gtk::Align::CENTER);
    close->signal_clicked().connect([this, find_tab_index]() {
        const int idx = find_tab_index();
        if (idx >= 0) close_tab(idx);
    });

    tab->append(*click_area);
    tab->append(*close);

    // Looks up are cheap and used on every refresh — stash the label and view
    // pointer on the widget itself so refresh_tab_widget can update in place.
    g_object_set_data(G_OBJECT(tab->gobj()), "remin-view", reinterpret_cast<gpointer>(view));
    g_object_set_data(G_OBJECT(tab->gobj()), "remin-label", reinterpret_cast<gpointer>(lbl));

    return tab;
}

void MainWindow::refresh_tab_widget(Gtk::Box* tab, size_t index) {
    if (!tab) return;
    TabView* view = tabs_[index].get();
    auto* lbl = reinterpret_cast<Gtk::Label*>(g_object_get_data(G_OBJECT(tab->gobj()), "remin-label"));

    if (static_cast<int>(index) == active_tab_) {
        if (!tab->has_css_class("active")) tab->add_css_class("active");
    } else {
        tab->remove_css_class("active");
    }

    if (lbl) {
        std::string label_text = view->title();
        if (view->kind() == TabKind::Note) {
            auto* note = static_cast<NoteTabView*>(view);
            if (note->is_modified()) label_text = "\u25CF " + label_text;
            if (!note->has_path()) label_text += "  (temp)";
        }
        if (lbl->get_text() != Glib::ustring(label_text)) lbl->set_text(label_text);
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
    // In replace mode the typed text must land in the REPLACE field, not the
    // find field — otherwise typing the replacement silently changes the
    // search term and invalidates the current match.
    if (replace)
        replace_entry_->grab_focus();
    else
        find_entry_->grab_focus();
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
    // Explicitly clear ALL search highlights first (every tab, every pane).
    // We must NOT rely on set_text() below to propagate through the widget
    // changed-signal chain — hiding the bar has to deterministically wipe every
    // surface's highlight, including the current tab's focused match.
    clear_all_search_highlights();
    // Clear both entries so stale text is not left and the next Ctrl+F opens
    // with a clean slate.
    find_entry_->set_text("");
    replace_entry_->set_text("");
    find_bar_->set_visible(false);
    replace_entry_->set_visible(false);
    replace_icon_->set_visible(false);
    replace_btn_->set_visible(false);
    replace_all_btn_->set_visible(false);
    find_match_label_->set_visible(false);
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
    // ESC closes find bar ONLY when find bar (or its children) is focused.
    // If editor/terminal is focused, ESC should not close find bar.
    if (keyval == GDK_KEY_Escape) {
        if (find_bar_->get_visible()) {
            // Check if focus is inside the find bar
            auto* focus_widget = get_focus();
            bool focus_in_find_bar = false;
            if (focus_widget) {
                // Walk up the widget hierarchy to see if find_bar_ is an ancestor
                for (auto* w = focus_widget; w; w = w->get_parent()) {
                    if (w == find_bar_) {
                        focus_in_find_bar = true;
                        break;
                    }
                }
            }
            if (focus_in_find_bar) {
                hide_find_bar();
                return true;
            }
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
    if (ctrl && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
        toggle_history_sidebar();
        return true;
    }
    if (ctrl && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
        close_tab(active_tab_);
        return true;
    }
    // Split shortcuts: Alt+H = horizontal, Alt+V = vertical
    if ((mods & Gdk::ModifierType::ALT_MASK) != Gdk::ModifierType(0) && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        on_split_terminal_horizontal();
        return true;
    }
    if ((mods & Gdk::ModifierType::ALT_MASK) != Gdk::ModifierType(0) && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        on_split_terminal_vertical();
        return true;
    }
    // Alt+K kills the focused terminal pane/tab.
    if ((mods & Gdk::ModifierType::ALT_MASK) != Gdk::ModifierType(0) && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        on_close_terminal_pane();
        return true;
    }
    return false;
}

void MainWindow::clear_all_search_highlights() {
    for (auto& tab : tabs_) {
        tab->clear_search();
    }
}

void MainWindow::sync_find_text() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;
    const auto text = find_entry_->get_text();
    // Empty means the user deleted the whole term: like Esc, wipe every search
    // highlight (not just the active tab's) so no tab keeps a stale match.
    if (text.empty()) {
        clear_all_search_highlights();
        update_find_match_label();
        return;
    }
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() == TabKind::Terminal) {
        if (auto* p = static_cast<TerminalTabView*>(tab)->focused_pane())
            p->set_search_text(text);
    } else if (tab->kind() == TabKind::Note) {
        if (auto* editor = static_cast<NoteTabView*>(tab)->editor())
            editor->set_search_text(text);
    }
    update_find_match_label();
}

void MainWindow::update_find_match_label() {
    if (!find_match_label_) return;
    if (!find_bar_ || !find_bar_->get_visible()) {
        find_match_label_->set_visible(false);
        return;
    }
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) {
        find_match_label_->set_visible(false);
        return;
    }
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() != TabKind::Note) {
        find_match_label_->set_visible(false);
        return;
    }
    auto* editor = static_cast<NoteTabView*>(tab)->editor();
    if (!editor || find_entry_->get_text().empty()) {
        find_match_label_->set_visible(false);
        return;
    }
    auto [cur, total] = editor->current_search_position();
    if (total <= 0) {
        find_match_label_->set_text("No results");
        find_match_label_->set_visible(true);
        return;
    }
    if (cur > 0)
        find_match_label_->set_text(std::to_string(cur) + " / " + std::to_string(total));
    else
        find_match_label_->set_text(std::to_string(total) + " matches");
    find_match_label_->set_visible(true);
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
        if (auto* editor = static_cast<NoteTabView*>(tab)->editor()) {
            editor->set_search_text(find_entry_->get_text());
            editor->search_next();
            update_find_match_label();
        }
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
        if (auto* editor = static_cast<NoteTabView*>(tab)->editor()) {
            editor->set_search_text(find_entry_->get_text());
            editor->search_previous();
            update_find_match_label();
        }
    }
}

void MainWindow::on_replace() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() == TabKind::Note) {
        auto* n = static_cast<NoteTabView*>(tab);
        if (auto* editor = n->editor()) {
            editor->set_replace_text(replace_entry_->get_text());
            editor->do_replace();
            update_find_match_label();
        }
    }
}

void MainWindow::on_replace_all() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size()) return;
    auto* tab = tabs_[active_tab_].get();
    if (tab->kind() == TabKind::Note) {
        auto* n = static_cast<NoteTabView*>(tab);
        if (auto* editor = n->editor()) {
            editor->set_replace_text(replace_entry_->get_text());
            editor->do_replace_all();
            update_find_match_label();
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
            if (directory_panel_) directory_panel_->refresh();
        }
        if (sidebar_toggle_btn_) {
            sidebar_toggle_btn_->set_icon_name("sidebar-hide-symbolic");
            sidebar_toggle_btn_->set_tooltip_text("Hide Sidebar (Ctrl+P)");
        }
    } else {
        main_paned_->set_position(0);
        if (sidebar_toggle_btn_) {
            sidebar_toggle_btn_->set_icon_name("sidebar-show-symbolic");
            sidebar_toggle_btn_->set_tooltip_text("Show Sidebar (Ctrl+P)");
        }
    }
}

void MainWindow::apply_initial_sidebar_state() {
    if (!main_paned_) return;

    const bool open = controller_ && controller_->auto_show_panel_enabled();
    sidebar_visible_ = open;
    main_paned_->set_position(open ? 220 : 0);

    if (sidebar_toggle_btn_) {
        sidebar_toggle_btn_->set_icon_name(open ? "sidebar-hide-symbolic" : "sidebar-show-symbolic");
        sidebar_toggle_btn_->set_tooltip_text(open ? "Hide Sidebar (Ctrl+P)" : "Show Sidebar (Ctrl+P)");
    }

    // Refresh if showing directory
    if (open && sidebar_stack_ && sidebar_stack_->get_visible_child_name() == "directory") {
        if (directory_panel_) directory_panel_->refresh();
    }
}

void MainWindow::on_settings() {
    auto dialog = Gtk::make_managed<SettingsDialog>(*this, controller_);
    dialog->set_transient_for(*this);
    dialog->present();
}

void MainWindow::open_note_from_path(const std::filesystem::path& path) {
    // Normalize so an equivalent path (e.g. with a trailing slash or relative
    // component) still matches an already-open tab.
    std::error_code ec;
    const auto canon = std::filesystem::weakly_canonical(path, ec);
    const std::string key = ec ? path.string() : canon.string();

    // 1) If this exact file is already open in a note tab, don't open a second
    //    tab — just focus it. If it has unsaved edits, ask the user what to do
    //    (the single exception to auto-reload: never silently discard edits).
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i]->kind() != TabKind::Note) continue;
        auto* note = static_cast<NoteTabView*>(tabs_[i].get());
        if (note->path() == key) {
            // Focus the existing tab.
            content_stack_->set_visible_child(*tabs_[i]);
            active_tab_ = static_cast<int>(i);
            update_tab_bar();
            update_toolbar();
            tabs_[i]->activate();
            update_status_bar();
            // Only prompt if there are unsaved edits.
            if (note->is_modified()) note->prompt_open_conflict();
            return;
        }
    }

    // 2) Not open — create a new tab and load the file from disk.
    auto noteId = controller_->new_note();
    if (noteId.empty()) return;

    auto* view = new NoteTabView(controller_, noteId);
    view->set_save_state_callback([this]() {
        update_tab_bar();
    });
    view->set_file_saved_callback([this](const std::filesystem::path& saved) {
        if (directory_panel_) directory_panel_->on_note_saved(saved);
    });
    auto idx = static_cast<int>(tabs_.size());
    tabs_.push_back(std::unique_ptr<TabView>(view));
    note_tabs_.push_back(view);

    content_stack_->add(*view, std::to_string(idx));
    content_stack_->set_visible_child(*view);
    active_tab_ = idx;

    // Load the file into the editor BEFORE refreshing the tab bar so the tab
    // label shows the file basename (and the note is bound to its path).
    view->load_file(path);

    update_tab_bar();
    update_toolbar();
    view->activate();
    update_status_bar();
}

void MainWindow::refresh_theme() {
    // AdwStyleManager applies its provider for the whole display; re-tagging
    // the window ensures CSS class is present for custom styling.
    if (!has_css_class("remin-window")) add_css_class("remin-window");
}

void MainWindow::on_terminal_color_profile() {
    if (active_tab_ < 0 || active_tab_ >= (int)tabs_.size() ||
        tabs_[active_tab_]->kind() != TabKind::Terminal) {
        return;
    }

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

    dialog->signal_response().connect([this, dialog, fg_btn, bg_btn](int response) {
        if (response == Gtk::ResponseType::OK) {
            Gdk::RGBA fg = fg_btn->get_rgba();
            Gdk::RGBA bg = bg_btn->get_rgba();
            // Apply to ALL terminal panes across ALL tabs
            for (auto& tab_ptr : tabs_) {
                if (tab_ptr->kind() == TabKind::Terminal) {
                    auto* term_tab = static_cast<TerminalTabView*>(tab_ptr.get());
                    term_tab->set_all_pane_colors(fg, bg);
                }
            }
            // Save to session controller for persistence
            if (controller_) {
                controller_->set_terminal_colors(fg.to_string(), bg.to_string());
            }
        }
        dialog->close();
    });
}

} // namespace remin::gui