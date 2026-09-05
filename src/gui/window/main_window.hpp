#pragma once

#include "core/autosave.hpp"
#include "core/workspace_core.hpp"
#include "gui/session/session_controller.hpp"
#include "gui/window/directory_tree_panel.hpp"
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
    void apply_initial_sidebar_state();
    void clear_history();
    void on_settings();
    void open_note_from_path(const std::filesystem::path& path);
    void on_custom_split();
    void refresh_theme();

    void update_tab_bar();
    // Persistent tab widgets: `build_tab_widget` creates a fresh tab container
    // when a new tab is appended; `refresh_tab_widget` only updates the label /
    // active class of an existing widget in place (so the unsaved-dot updates
    // live while typing instead of rebuilding the whole bar).
    Gtk::Box* build_tab_widget(size_t index);
    void refresh_tab_widget(Gtk::Box* tab, size_t index);
    void update_tab_overflow();
    void update_status_bar();
    void update_header();

    // Find bar dispatch
    void show_find_bar(bool replace = false);
    void hide_find_bar();
    void on_find_next();
    void on_find_prev();
    void on_replace();
    void on_replace_all();
    bool on_find_key_pressed(guint keyval, guint, Gdk::ModifierType mods);

    // Global sidebar (History / Directory)
    void setup_sidebar();
    void set_sidebar_mode(const std::string& mode);
    void update_history_sidebar();

    void restore_workspace();

    SessionController* controller_;
    remin::core::Autosaver* autosaver_;
    remin::core::WorkspaceCore* core_;

    // Header
    Gtk::HeaderBar* header_{nullptr};
    Gtk::Box* header_box_{nullptr};
    Gtk::Picture* logo_image_{nullptr};
    Gtk::Label* header_label_{nullptr};
    Gtk::Button* sidebar_toggle_btn_{nullptr};

    // Menu bar (using PopoverMenuBar)
    Gtk::PopoverMenuBar* menu_bar_{nullptr};

    // Toolbar below header (context-sensitive per active tab)
    Gtk::Box* toolbar_{nullptr};

    // Tab bar — scroller wraps the tab row; a dedicated horizontal scrollbar
    // lives in its own row BELOW the labels so it never overlaps them.
    Gtk::Box* tab_box_{nullptr};
    Gtk::ScrolledWindow* tab_scroller_{nullptr};
    Gtk::Scrollbar* tab_scrollbar_{nullptr};
    Gtk::Box* tab_bar_{nullptr};
    Gtk::Button* new_terminal_btn_{nullptr};
    Gtk::Button* new_note_btn_{nullptr};
    // Tab widgets in the same order as tabs_ (persistent across label updates).
    std::vector<Gtk::Box*> tab_widgets_;

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
    Gtk::Entry* replace_entry_{nullptr};
    Gtk::Button* find_next_{nullptr};
    Gtk::Button* find_prev_{nullptr};
    Gtk::Button* replace_btn_{nullptr};
    Gtk::Button* replace_all_btn_{nullptr};
    Gtk::Button* find_close_{nullptr};
    Gtk::Label* find_match_label_{nullptr};
    Gtk::Image* find_icon_{nullptr};
    Gtk::Image* replace_icon_{nullptr};

    // Autosave badge
    Gtk::Overlay* overlay_{nullptr};
    Gtk::Label* autosave_badge_{nullptr};
    sigc::connection autosave_badge_hide_;
    std::function<void()> update_tab_overflow_fn;

    // Global sidebar (History / Directory) — always visible, like VS Code
    Gtk::Paned* main_paned_{nullptr};
    bool sidebar_visible_{false};
    Gtk::Box* sidebar_root_{nullptr};
    Gtk::Stack* sidebar_stack_{nullptr};
    Gtk::ScrolledWindow* history_scroller_{nullptr};
    Gtk::Box* history_list_{nullptr};
    DirectoryTreePanel* directory_panel_{nullptr};
    Gtk::Button* sidebar_mode_tabs_[2]{nullptr, nullptr};
    std::vector<std::string> history_;

    // Key controller for accelerators
    Glib::RefPtr<Gtk::EventControllerKey> key_ctrl_;
};

} // namespace remin::gui