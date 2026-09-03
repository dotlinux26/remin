#pragma once

#include <gtkmm.h>
#include <string>

namespace remin::gui {

// Markdown preview pane: the note editor, when split, renders markdown to the
// side in a live preview.
//
// Parsing is done by md4c (CommonMark) — a tiny C library (~100 KB) — and the
// result is turned into Pango markup shown by a Gtk::Label inside a scrolled
// window. WebKitGTK is deliberately avoided so the app stays lightweight;
// headings, lists, code blocks, block quotes, emphasis, strikethrough and
// links are supported.
class MarkdownPreview : public Gtk::ScrolledWindow {
public:
    MarkdownPreview();
    void render(const std::string& markdown);

    // Pure, testable converter: markdown -> Pango markup string.
    static std::string to_pango(const std::string& markdown);

private:
    Gtk::Label* label_{nullptr};
};

} // namespace remin::gui
