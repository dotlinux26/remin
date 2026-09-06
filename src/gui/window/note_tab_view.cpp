#include "gui/window/note_tab_view.hpp"
#include "gui/session/session_controller.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace remin::gui {

NoteTabView::NoteTabView(SessionController* controller, const std::string& noteId)
    : controller_(controller), note_id_(noteId), title_("note") {
    editor_ = Gtk::make_managed<NoteEditor>(
        [this]() {
            if (controller_ && controller_->autosaver())
                controller_->autosaver()->note_note_activity(note_id_);
        });
    // Hold an extra ref on the editor so it survives being reparented between
    // content_host_ and content_split_ during preview toggles.
    g_object_ref(editor_->gobj());
    set_hexpand(true);
    set_vexpand(true);

    content_host_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    content_host_->set_hexpand(true);
    content_host_->set_vexpand(true);
    append(*content_host_);

    if (controller_) {
        const auto saved = controller_->load_note(noteId);
        if (!saved.empty()) {
            editor_->set_text(saved);
            editor_->set_modified(false);
        }
    }

    connect_editor();
    set_content(*editor_);
    start_watcher();
}

NoteTabView::~NoteTabView() {
    // glibmm timeouts are NOT auto-cancelled when the sigc::connection object
    // is destroyed: the GSource stays attached to the main context and keeps
    // dispatching into lambdas that captured `this`. After the destructor the
    // NoteTabView is freed, so a pending watcher/debounce firing 1.5s later is
    // a use-after-free. Disconnect both explicitly before touching any state.
    watcher_timer_.disconnect();
    dirty_debounce_.disconnect();
    // Release the extra ref we took on the editor in the constructor.
    if (editor_) {
        auto* g = editor_->gobj();
        editor_ = nullptr;
        g_object_unref(g);
    }
}

void NoteTabView::connect_editor() {
    if (!controller_) return;
    editor_->set_on_save([this]() {
        if (controller_->autosaver()) controller_->autosaver()->flush_now();
        save_now();
    });
    editor_->set_on_preview([this](const std::string& md) {
        if (preview_) preview_->render(md);
    });

    // Track modified state changes in real-time to update the dirty dot (●) immediately.
    // Connect to the TextBuffer's signal_modified_changed which fires specifically
    // when the modified state changes (clean <-> dirty), not on every keystroke.
    if (editor_) {
        auto buf = editor_->buffer();
        if (buf) {
            buf->signal_modified_changed().connect([this]() {
                bool now = editor_ && editor_->is_modified();
                if (now != last_modified_) {
                    last_modified_ = now;
                    notify_save_state();
                }
            });
            // Belt & suspenders: also watch plain content changes with a short
            // debounce so the unsaved dot appears live WHILE typing (some
            // buffers only flip the modified flag together with a user action).
            buf->signal_changed().connect([this]() {
                if (dirty_debounce_.connected()) dirty_debounce_.disconnect();
                dirty_debounce_ = Glib::signal_timeout().connect(
                    [this]() {
                        dirty_debounce_.disconnect();
                        bool now = editor_ && editor_->is_modified();
                        if (now != last_modified_) {
                            last_modified_ = now;
                            notify_save_state();
                        }
                        return false;
                    },
                    120);
            });
        }
    }
}

void NoteTabView::activate() {
    if (editor_) editor_->focus_editor();
}

void NoteTabView::deactivate() {}

bool NoteTabView::focus_search() {
    if (editor_) editor_->show_find(false);
    return true;
}

void NoteTabView::clear_search() {
    // Empty search term removes every Remin search tag (match + current-match)
    // and resets the navigation state.
    if (editor_) editor_->set_search_text("");
}

void NoteTabView::show_find_replace(bool show_replace) {
    if (editor_) {
        editor_->clear_find_replace_entries();
        editor_->show_find(show_replace);
    }
}

void NoteTabView::set_content(Gtk::Widget& content) {
    if (!content_host_) return;
    while (content_host_->get_first_child()) content_host_->remove(*content_host_->get_first_child());
    content_host_->append(content);
}

void NoteTabView::save_now() {
    // A temp note has no assigned file path. Pressing Save (Ctrl+S) must assign
    // one via Save As rather than silently writing to an opaque temp store.
    const auto path = controller_ ? controller_->note_path(note_id_) : std::string();
    if (path.empty()) {
        save_as();
        return;
    }

    // 1) Persist the body into the app's own storage (blob store).
    if (controller_ && controller_->autosaver()) controller_->autosaver()->flush_now();

    // 2) Mirror to the assigned on-disk file.
    if (!controller_) return;
    const auto body = text();
    if (!controller_->write_note_file(path, body)) {
        g_warning("remin: could not save note file: %s", path.c_str());
    }
    if (editor_) editor_->set_modified(false);
    notify_save_state();
    if (on_file_saved_) on_file_saved_(path);
}

void NoteTabView::save_as(std::function<void()> on_done) {
    if (!controller_) return;

    auto* root = get_root();
    auto* win = dynamic_cast<Gtk::Window*>(root);
    if (!win) return;

    auto dialog = Gtk::make_managed<Gtk::FileChooserDialog>(
        *win, "Save Note As…", Gtk::FileChooser::Action::SAVE);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Save", Gtk::ResponseType::OK);
    dialog->set_modal(true);
    dialog->set_current_folder(Gio::File::create_for_path(Glib::get_home_dir()));
    dialog->set_current_name((title_ == "note" ? "note" : title_) + ".md");
    dialog->signal_response().connect([this, dialog, on_done = std::move(on_done)](int response) {
        if (response == Gtk::ResponseType::OK) {
            const auto filename = dialog->get_file()->get_path();
            if (!filename.empty()) {
                controller_->set_note_path(note_id_, filename);
                if (!controller_->write_note_file(filename, editor_->text())) {
                    g_warning("remin: could not write note to %s", filename.c_str());
                }
                // Refresh the tab title to the chosen basename.
                set_title(Glib::path_get_basename(filename));
                if (editor_) editor_->set_modified(false);
                start_watcher();
                // Tab label now shows the real filename (no more "(temp)").
                notify_save_state();
                if (on_file_saved_) on_file_saved_(filename);
                if (on_done) on_done();
            }
        }
    
        dialog->close();
    });
    dialog->present();
}

void NoteTabView::toggle_preview() {
    if (preview_) {
        // Shut down the split, restore editor only.
        // IMPORTANT: detach children from the paned BEFORE destroying it,
        // otherwise the paned's destructor unparents and frees managed children.
        if (content_split_) {
            gtk_paned_set_start_child(GTK_PANED(content_split_->gobj()), nullptr);
            gtk_paned_set_end_child(GTK_PANED(content_split_->gobj()), nullptr);
        }
        content_split_ = nullptr;
        preview_ = nullptr;
        set_content(*editor_);
        return;
    }
    // Split horizontally: editor | preview inside the content host.
    content_split_ = Gtk::make_managed<Gtk::Paned>();
    content_split_->set_orientation(Gtk::Orientation::HORIZONTAL);
    content_split_->set_hexpand(true);
    content_split_->set_vexpand(true);
    preview_ = Gtk::make_managed<MarkdownPreview>();

    // A slim header above the preview: [Sync Scroll □] stays compact.
    auto* preview_host = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    auto* preview_header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    preview_header->add_css_class("remin-preview-header");
    auto* sync = Gtk::make_managed<Gtk::ToggleButton>("Sync Scroll");
    sync->add_css_class("remin-preview-toggle");
    sync->set_active(sync_scroll_);
    sync->signal_toggled().connect([this, sync]() {
        sync_scroll_ = sync->get_active();
        if (sync_scroll_) {
            // Immediately sync from editor to preview.
            if (preview_) on_editor_scroll();
        }
    });
    preview_header->append(*sync);
    preview_host->append(*preview_header);
    preview_host->append(*preview_);

    // Initial render.
    preview_->render(editor_->text());
    // Remove editor from content_host before reparenting it into the paned.
    set_content(*content_split_);
    content_split_->set_start_child(*editor_);
    content_split_->set_end_child(*preview_host);

    // Wire scroll sync (guarded against feedback loops).
    if (auto adj = editor_->vadjustment()) {
        adj->signal_value_changed().connect(
            [this]() { on_editor_scroll(); });
    }
    if (auto adj = preview_->vadjustment()) {
        adj->signal_value_changed().connect(
            [this]() { on_preview_scroll(); });
    }

    // Auto-split 50/50 (deterministic default). Set the position once the paned
    // has been allocated so the half-point tracks the actual width.
    Glib::signal_idle().connect_once([this]() {
        if (!content_split_) return;
        int total = content_split_->get_width();
        if (total > 0) {
            content_split_->set_position(total / 2);
        }
    });
}

void NoteTabView::on_editor_scroll() {
    if (!sync_scroll_ || !preview_ || syncing_) return;
    auto sadj = editor_->vadjustment();
    auto padv = preview_->vadjustment();
    if (!sadj || !padv) return;
    double upper = sadj->get_upper() - sadj->get_page_size();
    if (upper <= 0.0) return;
    double frac = sadj->get_value() / upper;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    syncing_ = true;
    preview_->set_scroll_fraction(frac);
    syncing_ = false;
}

void NoteTabView::on_preview_scroll() {
    if (!sync_scroll_ || !preview_ || syncing_) return;
    auto sadj = editor_->vadjustment();
    auto padv = preview_->vadjustment();
    if (!sadj || !padv) return;
    double upper = padv->get_upper() - padv->get_page_size();
    if (upper <= 0.0) return;
    double frac = padv->get_value() / upper;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    syncing_ = true;
    double target = frac * (sadj->get_upper() - sadj->get_page_size());
    sadj->set_value(target);
    syncing_ = false;
}

void NoteTabView::start_watcher() {
    if (!controller_) return;
    const auto path = controller_->note_path(note_id_);
    if (path.empty()) return;

    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status)) return;

    watched_path_ = path;
    const auto mtime = std::filesystem::last_write_time(path, ec);
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return;
    last_mtime_ = mtime;
    last_size_ = size;
    triggered_ = false;

    if (!watcher_timer_.connected()) {
        watcher_timer_ = Glib::signal_timeout().connect(
            [this]() -> bool { return poll_file(); }, 1500);
    }
}

bool NoteTabView::poll_file() {
    // If no path is assigned (e.g. never saved as a file) there is nothing to
    // watch; keep the timer alive in case the user later does Save As.
    const auto path = controller_ ? controller_->note_path(note_id_) : std::string();
    if (path.empty() || path != watched_path_) {
        start_watcher();
        return true;
    }
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status)) return true;
    const auto mtime = std::filesystem::last_write_time(path, ec);
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return true;

    if (mtime == last_mtime_ && size == last_size_) {
        return true; // unchanged
    }
    last_mtime_ = mtime;
    last_size_ = size;

    if (triggered_) return true;
    triggered_ = true;

    if (controller_ && controller_->auto_reload_enabled()) {
        reload_from_disk();
    } else {
        prompt_reload();
    }
    return true;
}

bool NoteTabView::file_matches_snapshot() {
    // True when the on-disk file is byte-identical to the editor buffer, so we
    // can skip the prompt if the change we saw is actually our own write.
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(watched_path_, ec);
    const auto size = std::filesystem::file_size(watched_path_, ec);
    if (ec) return false;
    if (mtime != last_mtime_ || size != last_size_) return false;
    std::ifstream in(watched_path_, std::ios::binary);
    std::string disk((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return disk == editor_->text();
}

void NoteTabView::reload_from_disk() {
    std::ifstream in(watched_path_, std::ios::binary);
    if (!in) return;
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    if (content == editor_->text()) return;
    editor_->set_text(content);
    triggered_ = false;

}

void NoteTabView::prompt_reload() {
    if (!get_realized()) return;
    auto* root = get_root();
    auto* win = dynamic_cast<Gtk::Window*>(root);
    if (!win) return;

    auto dialog = Gtk::make_managed<Gtk::MessageDialog>(
        *win, "File changed on disk",
        false, Gtk::MessageType::QUESTION, Gtk::ButtonsType::NONE, false);
    dialog->set_secondary_text("The file \"" + watched_path_ +
                               "\" was modified externally. Reload it, or keep "
                               "your current editor contents (you can save manually later).");
    auto reload_btn = dialog->add_button("_Reload", Gtk::ResponseType::OK);
    auto keep_btn = dialog->add_button("_Keep", Gtk::ResponseType::CANCEL);
    (void)reload_btn; (void)keep_btn;
    dialog->set_modal(true);
    dialog->signal_response().connect([this, dialog](int response) {
        if (response == Gtk::ResponseType::OK) {
            reload_from_disk();
        }
        triggered_ = false;
        dialog->close();
    });
    dialog->present();
}

void NoteTabView::load_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (file) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        editor_->set_text(content);
        editor_->set_modified(false);
        // Bind the note to this file so explicit save (Ctrl+S) writes back to
        // the same path (fix: previously the note had no path -> save went to a
        // temp store and reopening showed stale content).
        if (controller_) controller_->set_note_path(note_id_, path.string());
        set_title(path.filename().string());
        notify_save_state();
        // Set the watched path so it can detect changes
        watched_path_ = path;
        last_mtime_ = std::filesystem::last_write_time(path);
        last_size_ = std::filesystem::file_size(path);
        start_watcher();
    }

}

bool NoteTabView::is_modified() const {
    return editor_ && editor_->is_modified();
}

bool NoteTabView::has_path() const {
    return controller_ && !controller_->note_path(note_id_).empty();
}

std::string NoteTabView::path() const {
    const auto p = controller_ ? controller_->note_path(note_id_) : std::string();
    if (p.empty()) return p;
    // Canonicalize so two spellings of the same file compare equal (used for
    // the "already open, don't open a second tab" check). Fall back to the raw
    // path if it doesn't resolve (the note may be saved to disk shortly).
    std::error_code ec;
    const auto canon = std::filesystem::weakly_canonical(p, ec);
    return (ec || canon.empty()) ? p : canon.string();
}

void NoteTabView::prompt_open_conflict() {
    auto* root = get_root();
    auto* win = dynamic_cast<Gtk::Window*>(root);
    if (!win) return;

    // Called when the user explicitly re-opens THIS file from the directory
    // tree while the note has unsaved edits. This is the single exception to
    // auto-reload: we never silently discard the user's work here.
    auto dialog = Gtk::make_managed<Gtk::MessageDialog>(
        *win, "Save changes?",
        false, Gtk::MessageType::QUESTION, Gtk::ButtonsType::NONE, false);
    dialog->set_secondary_text(
        "This file has unsaved changes. Save them before reloading the file "
        "from disk, or discard your edits and reload (a reload would otherwise "
        "overwrite your current editor content).");
    auto save_btn = dialog->add_button("_Save", Gtk::ResponseType::YES);
    auto discard_btn = dialog->add_button("_Discard & Reload", Gtk::ResponseType::NO);
    auto cancel_btn = dialog->add_button("_Cancel", Gtk::ResponseType::CANCEL);
    (void)save_btn; (void)discard_btn; (void)cancel_btn;
    dialog->set_modal(true);
    dialog->signal_response().connect([this, dialog](int response) {
        if (response == Gtk::ResponseType::YES) {
            save_now();          // writes buffer to the bound path
            reload_from_disk();  // sync to what is actually on disk
        } else if (response == Gtk::ResponseType::NO) {
            reload_from_disk();  // discard edits, read disk
        }
        triggered_ = false;
        dialog->close();
    });
    dialog->present();
}

void NoteTabView::notify_save_state() {
    if (on_save_state_) on_save_state_();
}

// --- Runtime state capture/restore (design §7) ---

NoteTabView::State NoteTabView::capture_state() const {
    State s;
    s.content = editor_ ? editor_->text() : std::string();
    if (editor_) {
        auto buf = editor_->buffer();
        if (buf) {
            auto iter = buf->get_insert()->get_iter();
            s.cursor_offset = iter.get_offset();
        }
        if (auto adj = editor_->vadjustment()) {
            double upper = adj->get_upper() - adj->get_page_size();
            if (upper > 0.0) {
                s.scroll_fraction = adj->get_value() / upper;
            }
        }
    }
    s.preview_enabled = preview_ != nullptr;
    if (content_split_) {
        int total = content_split_->get_width();
        if (total > 0) {
            s.split_ratio = static_cast<double>(content_split_->get_position()) / total;
        }
    }
    s.sync_scroll = sync_scroll_;
    return s;
}

void NoteTabView::restore_state(const State& state) {
    if (!editor_) return;
    editor_->set_text(state.content);
    editor_->set_modified(false);

    // Restore cursor position
    if (state.cursor_offset >= 0) {
        auto buf = editor_->buffer();
        if (buf) {
            auto iter = buf->get_iter_at_offset(std::min(state.cursor_offset, static_cast<int>(buf->get_char_count())));
            buf->place_cursor(iter);
        }
    }

    // Restore scroll position
    if (state.scroll_fraction >= 0.0 && state.scroll_fraction <= 1.0) {
        if (auto adj = editor_->vadjustment()) {
            double upper = adj->get_upper() - adj->get_page_size();
            if (upper > 0.0) {
                adj->set_value(state.scroll_fraction * upper);
            }
        }
    }

    // Restore preview state
    bool want_preview = state.preview_enabled;
    bool have_preview = preview_ != nullptr;
    if (want_preview != have_preview) {
        toggle_preview();
    }
    if (preview_ && content_split_) {
        int total = content_split_->get_width();
        if (total > 0) {
            content_split_->set_position(static_cast<int>(state.split_ratio * total));
        }
    }
    sync_scroll_ = state.sync_scroll;
    if (sync_scroll_ && preview_) {
        // Immediately sync scroll from editor to preview
        on_editor_scroll();
    }
}

} // namespace remin::gui
