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
    // Text block handling
    bool in_paragraph = false;
    bool in_heading = false;
    // List handling
    int list_ordinal = 1;       // next number for ordered list items
    bool list_ordered = false;  // current list type
    bool in_list_item = false;
    bool task_item = false;     // current list item is a task item
    bool task_checked = false;  // current task item is checked
    // Table handling
    bool in_table = false;
    bool in_table_header = false;
    bool first_cell_in_row = true;
};

int enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto& R = *static_cast<RenderState*>(userdata);
    switch (type) {
        case MD_BLOCK_H: {
            auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            R.out += "\n<span size=\"" + std::string(heading_size(static_cast<int>(h->level))) + "\"><b>";
            R.in_heading = true;
            break;
        }
        case MD_BLOCK_P:
            R.out += "\n";
            R.in_paragraph = true;
            break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: {
            R.list_ordered = (type == MD_BLOCK_OL);
            if (type == MD_BLOCK_OL) {
                auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
                R.list_ordinal = (ol && ol->start > 0) ? static_cast<int>(ol->start) : 1;
            } else {
                R.list_ordinal = 1;
            }
            R.out += "\n";
            break;
        }
        case MD_BLOCK_LI: {
            auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            R.task_item = (li && li->is_task);
            R.task_checked = (li && (li->task_mark == 'x' || li->task_mark == 'X'));
            R.in_list_item = true;
            std::string marker;
            if (R.task_item) {
                marker = R.task_checked ? "\u2611" : "\u2610";  // ☑ / ☐
                marker += " ";
            } else if (R.list_ordered) {
                marker = std::to_string(R.list_ordinal++) + ". ";
            } else {
                marker = "\u2022 ";  // •
            }
            R.out += " " + marker;
            break;
        }
        case MD_BLOCK_CODE:
            R.out += "\n<span font_family=\"monospace\">";
            R.in_pre = true;
            break;
        case MD_BLOCK_QUOTE:
            R.out += "\n<i>\u275d ";
            break;
        case MD_BLOCK_HR:
            R.out += "\n<span foreground=\"gray\">\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500</span>\n";
            break;
        case MD_BLOCK_TABLE: {
            R.in_table = true;
            R.out += "\n";
            break;
        }
        case MD_BLOCK_THEAD:
            R.in_table_header = true;
            break;
        case MD_BLOCK_TR: {
            R.first_cell_in_row = true;
            R.out += "<span font_family=\"monospace\">| ";
            break;
        }
        case MD_BLOCK_TD:
        case MD_BLOCK_TH: {
            if (!R.first_cell_in_row) R.out += " | ";
            if (R.in_table_header) R.out += "<b>";
            R.first_cell_in_row = false;
            break;
        }
        default:
            break;
    }
    return 0;
}

int leave_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto& R = *static_cast<RenderState*>(userdata);
    (void)detail;
    switch (type) {
        case MD_BLOCK_H: R.out += "</b></span>\n\n"; R.in_heading = false; break;
        case MD_BLOCK_P: R.out += "\n\n"; R.in_paragraph = false; break;
        case MD_BLOCK_LI: R.out += "\n"; R.in_list_item = false; break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: R.out += "\n"; break;
        case MD_BLOCK_CODE: R.out += "</span>\n\n"; R.in_pre = false; break;
        case MD_BLOCK_QUOTE: R.out += "</i>\n\n"; break;
        case MD_BLOCK_TABLE: R.out += "\n"; R.in_table = false; break;
        case MD_BLOCK_THEAD: R.in_table_header = false; break;
        case MD_BLOCK_TR: R.out += " |</span>\n"; break;
        case MD_BLOCK_TD:
        case MD_BLOCK_TH:
            if (R.in_table_header) R.out += "</b>";
            break;
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
        case MD_SPAN_A: {
            auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
            R.out += "<span underline=\"single\" foreground=\"#818cf8\">";
            (void)a;
            break;
        }
        case MD_SPAN_IMG:
            R.out += "[img] ";
            break;
        default:
            break;
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
        case MD_SPAN_A: R.out += "</span>"; break;
        case MD_SPAN_IMG: break;
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

void MarkdownPreview::set_scroll_fraction(double fraction) {
    auto adj = get_vadjustment();
    if (!adj) return;
    double upper = adj->get_upper() - adj->get_page_size();
    if (upper <= 0.0) return;
    adj->set_value(fraction * upper);
}

void MarkdownPreview::render(const std::string& markdown) {
    try {
        label_->set_markup(to_pango(markdown));
    } catch (const Glib::MarkupError&) {
        // Markup failed; fall back to escaped plain text.
        auto escaped = Glib::Markup::escape_text(markdown);
        label_->set_markup(escaped);
    }
}

} // namespace remin::gui
