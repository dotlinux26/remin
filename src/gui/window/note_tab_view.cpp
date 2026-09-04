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
    set_hexpand(true);
    set_vexpand(true);

    content_host_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    content_host_->set_hexpand(true);
    content_host_->set_vexpand(true);
    append(*content_host_);

    if (controller_) {
        const auto saved = controller_->load_note(noteId);
        if (!saved.empty()) editor_->set_text(saved);
    }

    connect_editor();
    set_content(*editor_);
    start_watcher();
}

NoteTabView::~NoteTabView() = default;

void NoteTabView::connect_editor() {
    if (!controller_) return;
    editor_->set_on_save([this]() {
        if (controller_->autosaver()) controller_->autosaver()->flush_now();
        save_now();
    });
    editor_->set_on_preview([this](const std::string& md) {
        if (preview_) preview_->render(md);
    });
}

void NoteTabView::activate() {
    if (editor_) editor_->focus_editor();
}

void NoteTabView::deactivate() {}

bool NoteTabView::focus_search() {
    if (editor_) editor_->show_find(false);
    return true;
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
    // 1) Persist the body into the app's own storage (blob store).
    if (controller_ && controller_->autosaver()) controller_->autosaver()->flush_now();

    // 2) Mirror to an on-disk file: the assigned path if any, otherwise the
    //    auto temp file (only when the autosave-to-temp setting is enabled).
    if (!controller_) return;
    const auto body = text();
    const auto path = controller_->note_path(note_id_);
    if (!path.empty()) {
        controller_->write_note_file(path, body);
    } else if (controller_->autosave_temp_enabled()) {
        controller_->write_note_file(controller_->note_temp_path(note_id_), body);
    }

}

void NoteTabView::save_as() {
    if (!controller_) return;

    auto* root = get_root();
    auto* win = dynamic_cast<Gtk::Window*>(root);
    if (!win) return;

    auto dialog = Gtk::make_managed<Gtk::FileChooserDialog>(
        *win, "Save Note As…", Gtk::FileChooser::Action::SAVE);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Save", Gtk::ResponseType::OK);
    dialog->set_modal(true);
    dialog->set_current_name((title_ == "note" ? "note" : title_) + ".md");
    dialog->signal_response().connect([this, dialog](int response) {
        if (response == Gtk::ResponseType::OK) {
            const auto filename = dialog->get_file()->get_path();
            if (!filename.empty()) {
                controller_->set_note_path(note_id_, filename);
                if (!controller_->write_note_file(filename, editor_->text())) {
                    g_warning("remin: could not write note to %s", filename.c_str());
                }
                // Refresh the tab title to the chosen basename.
                set_title(Glib::path_get_basename(filename));
                start_watcher();
            }
        }
    
        dialog->close();
    });
    dialog->present();
}

void NoteTabView::toggle_preview() {
    if (preview_) {
        // Shut down the split, restore editor only.
        content_split_ = nullptr;
        preview_ = nullptr;
        set_content(*editor_);
        return;
    }
    // Split horizontally: editor | preview inside the content host.
    content_split_ = Gtk::make_managed<Gtk::Paned>();
    content_split_->set_orientation(Gtk::Orientation::HORIZONTAL);
    preview_ = Gtk::make_managed<MarkdownPreview>();
    // Initial render.
    preview_->render(editor_->text());
    content_split_->set_start_child(*editor_);
    content_split_->set_end_child(*preview_);
    set_content(*content_split_);
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
        set_title(path.filename().string());
        // Set the watched path so it can detect changes
        watched_path_ = path;
        last_mtime_ = std::filesystem::last_write_time(path);
        last_size_ = std::filesystem::file_size(path);
        start_watcher();
    }

}

} // namespace remin::gui
