#pragma once

#include <gtkmm.h>
#include <functional>
#include <string>

namespace remin::gui {

// A note editor tab (edit surface only).
//
// Per the View → Controller → Core boundary, the editor never writes storage
// directly. It reports activity through on_change_ (which the session wires to
// the unified autosaver) and requests explicit saves via on_save_ (the session
// flushes the autosaver immediately). Markdown preview feeds on_preview_,
// debounced ~200 ms, so the parser/renderer is not hit on every keystroke.
//
// Includes: line-number gutter, find (Ctrl+F) + replace (Ctrl+H), type/edit.
class NoteEditor : public Gtk::Box {
public:
    // on_change is called after every buffer edit (activity signal).
    explicit NoteEditor(std::function<void()> on_change = {});

    [[nodiscard]] std::string text() const;
    void set_text(const std::string& text);

    void focus_editor() { view_->grab_focus(); }

    // Show the find/replace bar and focus its entry (Ctrl+F / Ctrl+H).
    void show_find();

    // Ctrl+S explicit-save hook (the session flushes the autosaver).
    void set_on_save(std::function<void()> on_save) { on_save_ = std::move(on_save); }
    // Live preview hook, debounced ~200 ms, with the current text.
    void set_on_preview(std::function<void(const std::string&)> cb) {
        on_preview_ = std::move(cb);
    }

    void request_save() { if (on_save_) on_save_(); }

private:
    void on_buffer_changed();
    void update_line_numbers();
    void update_gutter_width();
    void do_find_next(bool backwards = false);
    void do_replace();
    void do_replace_all();
    bool on_preview_tick();

    std::function<void()> on_change_;
    std::function<void()> on_save_;
    std::function<void(const std::string&)> on_preview_;

    Gtk::ScrolledWindow* scroller_{nullptr};
    Gtk::TextView* view_{nullptr};
    Gtk::TextView* gutter_{nullptr};
    Glib::RefPtr<Gtk::TextBuffer> buffer_;
    Glib::RefPtr<Gtk::TextBuffer> gutter_buffer_;

    Gtk::Box* find_bar_{nullptr};
    Gtk::Entry* find_entry_{nullptr};
    Gtk::Entry* replace_entry_{nullptr};

    sigc::connection preview_timer_;
    bool preview_pending_{false};
};

} // namespace remin::gui
