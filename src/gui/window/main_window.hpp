#pragma once

#include "core/autosave.hpp"
#include "core/workspace_core.hpp"
#include "gui/session/session_controller.hpp"
#include "gui/theme/theme_manager.hpp"
#include "gui/window/note_tab_view.hpp"
#include "gui/window/tab_view.hpp"
#include "gui/window/terminal_tab_view.hpp"

#include <gtkmm.h>
#include <memory>
#include <vector>
#include <string>

namespace remin::gui {

class MainWindow : public Gtk::Window {
public:
    MainWindow(SessionController* controller,
               remin::core::Autosaver* autosaver,
               remin::core::WorkspaceCore* core);
    ~MainWindow() override;

    // Accessors for Application to wire autosaver providers
    const std::vector<TerminalTabView*>& terminal_tabs() const { return term_tabs_; }
    const std::vector<NoteTabView*>& note_tabs() const { return note_tabs_; }

    void show_autosave_badge(bool success);
    void set_theme(ThemeManager* theme) { theme_ = theme; }

private:
    void setup_header();
    void setup_menu_bar();
    void setup_toolbar();
    void setup_tab_bar();
    void setup_content_stack();
    void setup_status_bar();
    void setup_find_bar();
    void update_toolbar();

    void new_terminal_tab();
    void new_note_tab();
    void close_tab(int index);
    void on_tab_switched(int page);
    void on_rename_window();
    void on_rename_tab(int index);
    void on_split_terminal_horizontal();
    void on_split_terminal_vertical();
    void on_close_terminal_pane();
    void on_toggle_note_preview();
    void on_note_save_as();
    void on_terminal_color_profile();
    void toggle_history_sidebar();
    void clear_history();
    void refresh_theme();

    void update_tab_bar();
    void update_status_bar();
    void update_header();

    // Find bar dispatch
    void show_find_bar();
    void hide_find_bar();
    void on_find_next();
    void on_find_prev();
    bool on_find_key_pressed(guint keyval, guint, Gdk::ModifierType mods);

    SessionController* controller_;
    remin::core::Autosaver* autosaver_;
    remin::core::WorkspaceCore* core_;

    // Header
    Gtk::HeaderBar* header_{nullptr};
    Gtk::Box* header_box_{nullptr};
    Gtk::Image* logo_image_{nullptr};
    Gtk::Label* header_label_{nullptr};

    // Menu bar (using PopoverMenuBar)
    Gtk::PopoverMenuBar* menu_bar_{nullptr};

    // Toolbar below header (context-sensitive per active tab)
    Gtk::Box* toolbar_{nullptr};

    // Theme
    ThemeManager* theme_{nullptr};

    // Tab bar
    Gtk::Box* tab_bar_{nullptr};
    Gtk::Button* new_terminal_btn_{nullptr};
    Gtk::Button* new_note_btn_{nullptr};

    // Content stack
    Gtk::Stack* content_stack_{nullptr};
    std::vector<std::unique_ptr<TabView>> tabs_;
    std::vector<TerminalTabView*> term_tabs_;
    std::vector<NoteTabView*> note_tabs_;
    int active_tab_{-1};

    // Status bar
    Gtk::Box* status_bar_{nullptr};
    Gtk::Label* status_label_{nullptr};

    // Find bar (overlay)
    Gtk::Box* find_bar_{nullptr};
    Gtk::Entry* find_entry_{nullptr};
    Gtk::Button* find_next_{nullptr};
    Gtk::Button* find_prev_{nullptr};
    Gtk::Button* find_close_{nullptr};

    // Autosave badge
    Gtk::Overlay* overlay_{nullptr};
    Gtk::Label* autosave_badge_{nullptr};
    sigc::connection autosave_badge_hide_;

    // Key controller for accelerators
    Glib::RefPtr<Gtk::EventControllerKey> key_ctrl_;
};

} // namespace remin::gui