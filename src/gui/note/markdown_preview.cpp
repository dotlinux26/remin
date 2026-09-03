#include "gui/note/markdown_preview.hpp"

#include <md4c.h>

#include <string>

namespace remin::gui {

namespace {

#ifndef REMIN_HAVE_MD4C
#error "md4c is required for the markdown preview"
#endif

std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

const char* heading_size(int level) {
    switch (level) {
        case 1: return "xx-large";
        case 2: return "x-large";
        case 3: return "large";
        case 4: return "medium";
        default: return "medium";
    }
}

struct RenderState {
    std::string out;
    bool in_pre = false;
    bool code_span = false;
};

int enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto& R = *static_cast<RenderState*>(userdata);
    switch (type) {
        case MD_BLOCK_H: {
            auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            R.out += "\n<span size=\"" + std::string(heading_size(h->level)) + "\"><b>";
            break;
        }
        case MD_BLOCK_P:
            R.out += "\n";
            break;
        case MD_BLOCK_LI:
            R.out += "  •  ";
            break;
        case MD_BLOCK_CODE:
            R.out += "\n<span font_family=\"monospace\">";
            R.in_pre = true;
            break;
        case MD_BLOCK_QUOTE:
            R.out += "\n<i>❝ ";
            break;
        case MD_BLOCK_HR:
            R.out += "\n<span foreground=\"gray\">──────────────</span>\n";
            break;
        default:
            break;
    }
    return 0;
}

int leave_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto& R = *static_cast<RenderState*>(userdata);
    (void)detail;
    switch (type) {
        case MD_BLOCK_H: R.out += "</b></span>\n\n"; break;
        case MD_BLOCK_P: R.out += "\n\n"; break;
        case MD_BLOCK_LI: R.out += "\n"; break;
        case MD_BLOCK_CODE: R.out += "</span>\n\n"; R.in_pre = false; break;
        case MD_BLOCK_QUOTE: R.out += "</i>\n\n"; break;
        default: break;
    }
    return 0;
}

int enter_span(MD_SPANTYPE type, void* detail, void* userdata) {
    auto& R = *static_cast<RenderState*>(userdata);
    (void)detail;
    switch (type) {
        case MD_SPAN_EM: R.out += "<i>"; break;
        case MD_SPAN_STRONG: R.out += "<b>"; break;
        case MD_SPAN_CODE: R.out += "<span font_family=\"monospace\">"; R.code_span = true; break;
        case MD_SPAN_DEL: R.out += "<s>"; break;
        case MD_SPAN_A: R.out += "<u>"; break;
        default: break;
    }
    return 0;
}

int leave_span(MD_SPANTYPE type, void* detail, void* userdata) {
    auto& R = *static_cast<RenderState*>(userdata);
    (void)detail;
    switch (type) {
        case MD_SPAN_EM: R.out += "</i>"; break;
        case MD_SPAN_STRONG: R.out += "</b>"; break;
        case MD_SPAN_CODE: R.out += "</span>"; R.code_span = false; break;
        case MD_SPAN_DEL: R.out += "</s>"; break;
        case MD_SPAN_A: R.out += "</u>"; break;
        default: break;
    }
    return 0;
}

int text_cb(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto& R = *static_cast<RenderState*>(userdata);
    const std::string chunk(text, size);
    switch (type) {
        case MD_TEXT_NORMAL:
            R.out += xml_escape(chunk);
            break;
        case MD_TEXT_BR:
            R.out += "\n";
            break;
        case MD_TEXT_SOFTBR:
            R.out += " ";
            break;
        default:
            // MD_TEXT_HTML / entities etc: drop raw HTML for safety.
            break;
    }
    return 0;
}

} // namespace

std::string MarkdownPreview::to_pango(const std::string& markdown) {
    RenderState R;
    MD_PARSER parser{};
    parser.flags = static_cast<unsigned>(
        MD_FLAG_COLLAPSEWHITESPACE |
        MD_FLAG_PERMISSIVEATXHEADERS |
        MD_FLAG_TABLES |
        MD_FLAG_STRIKETHROUGH |
        MD_FLAG_TASKLISTS |
        MD_FLAG_PERMISSIVEURLAUTOLINKS);
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text_cb;

    md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &R);
    return R.out;
}

MarkdownPreview::MarkdownPreview() {
    set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    set_hexpand(true);
    set_vexpand(true);

    label_ = Gtk::make_managed<Gtk::Label>();
    label_->set_wrap(true);
    label_->set_xalign(0.0);
    label_->set_yalign(0.0);
    label_->set_margin(8);
    label_->set_selectable(true);
    set_child(*label_);
}

void MarkdownPreview::render(const std::string& markdown) {
    try {
        label_->set_markup(to_pango(markdown));
    } catch (const Glib::Error&) {
        label_->set_text(markdown);
    } catch (const Glib::MarkupError&) {
        label_->set_text(markdown);
    }
}

} // namespace remin::gui
