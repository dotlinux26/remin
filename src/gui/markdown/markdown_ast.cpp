#include "gui/markdown/markdown_ast.hpp"

#include <md4c.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <list>
#include <map>

namespace remin::markdown {

namespace {

// ---- Entity decoding ------------------------------------------------------

std::string decode_numeric_entity(const std::string& digits, bool hex) {
    unsigned long code = 0;
    try {
        code = hex ? std::stoul(digits, nullptr, 16) : std::stoul(digits, nullptr, 10);
    } catch (...) {
        return "";
    }
    if (code == 0 || code > 0x10FFFF) return "";
    // Basic UTF-8 encode of the code point.
    std::string out;
    if (code <= 0x7F) {
        out += static_cast<char>(code);
    } else if (code <= 0x7FF) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

} // namespace

std::string decode_entity(const std::string& raw) {
    if (raw.size() < 3 || raw.front() != '&' || raw.back() != ';') return raw;
    const std::string body = raw.substr(1, raw.size() - 2);
    if (!body.empty() && body[0] == '#') {
        bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
        const std::string digits = hex ? body.substr(2) : body.substr(1);
        const std::string decoded = decode_numeric_entity(digits, hex);
        return decoded.empty() ? raw : decoded;
    }
    static const std::map<std::string, std::string, std::less<>> kNamed = {
        {"amp", "&"},  {"lt", "<"},  {"gt", ">"},   {"quot", "\""},
        {"apos", "'"}, {"nbsp", "\xC2\xA0"}, {"copy", "\xC2\xA9"},
        {"reg", "\xC2\xAE"}, {"hellip", "\xE2\x80\xA6"}, {"mdash", "\xE2\x80\x94"},
        {"ndash", "\xE2\x80\x93"}, {"lsquo", "\xE2\x80\x98"}, {"rsquo", "\xE2\x80\x99"},
        {"ldquo", "\xE2\x80\x9C"}, {"rdquo", "\xE2\x80\x9D"}, {"times", "\xC3\x97"},
        {"divide", "\xC3\xB7"}, {"plusmn", "\xC2\xB1"}, {"middot", "\xC2\xB7"},
    };
    auto it = kNamed.find(body);
    return it != kNamed.end() ? it->second : raw;
}

std::string slugify_heading(const std::string& text) {
    std::string slug;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!slug.empty() && slug.back() != '-') {
            if (c == ' ' || c == '\t' || c == '/') slug += '-';
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug = "section";
    return slug;
}

namespace {

// ---- AST builder (arena) --------------------------------------------------

struct ArenaNode {
    Node data;
    std::vector<ArenaNode*> children;
};

struct Builder {
    std::deque<ArenaNode> pool;
    std::vector<ArenaNode*> stack;
    ArenaNode* root = nullptr;

    ArenaNode* add(NodeType t, ArenaNode* parent) {
        pool.push_back(ArenaNode{});
        ArenaNode* n = &pool.back();
        n->data.type = t;
        if (parent) parent->children.push_back(n);
        return n;
    }
    ArenaNode* open(NodeType t) {
        ArenaNode* parent = stack.empty() ? root : stack.back();
        ArenaNode* n = add(t, parent);
        stack.push_back(n);
        return n;
    }
    ArenaNode* leaf(NodeType t) {
        ArenaNode* parent = stack.empty() ? root : stack.back();
        return add(t, parent);
    }
    void close() {
        if (!stack.empty()) stack.pop_back();
    }
    ArenaNode* top() const { return stack.empty() ? nullptr : stack.back(); }

    Node to_value(const ArenaNode* a) const {
        Node n = a->data;
        n.children.reserve(a->children.size());
        for (const ArenaNode* c : a->children) n.children.push_back(to_value(c));
        return n;
    }
};

// true when the current open node accumulates verbatim text (code/html).
bool verbatim(const ArenaNode* n) {
    return n && (n->data.type == NodeType::CodeBlock ||
                 n->data.type == NodeType::CodeSpan ||
                 n->data.type == NodeType::HtmlBlock);
}

CellAlign to_cell_align(MD_ALIGN a) {
    switch (a) {
        case MD_ALIGN_LEFT: return CellAlign::Left;
        case MD_ALIGN_CENTER: return CellAlign::Center;
        case MD_ALIGN_RIGHT: return CellAlign::Right;
        default: return CellAlign::None;
    }
}

int on_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_BLOCK_DOC:
            if (!b.root) {
                b.stack.clear();
                b.pool.push_back(ArenaNode{});
                b.root = &b.pool.back();
                b.root->data.type = NodeType::Root;
                b.stack.push_back(b.root);
            }
            break;
        case MD_BLOCK_H: {
            auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            auto* n = b.open(NodeType::Heading);
            n->data.level = static_cast<int>(h->level);
            break;
        }
        case MD_BLOCK_P: b.open(NodeType::Paragraph); break;
        case MD_BLOCK_QUOTE: b.open(NodeType::BlockQuote); break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: {
            auto* n = b.open(NodeType::List);
            n->data.ordered = (type == MD_BLOCK_OL);
            if (type == MD_BLOCK_OL) {
                auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
                n->data.start = (ol && ol->start > 0) ? static_cast<int>(ol->start) : 1;
                n->data.tight = ol && ol->is_tight;
            }
            break;
        }
        case MD_BLOCK_LI: {
            auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            auto* n = b.open(NodeType::ListItem);
            if (li) {
                n->data.task = li->is_task != 0;
                n->data.checked = (li->task_mark == 'x' || li->task_mark == 'X');
            }
            break;
        }
        case MD_BLOCK_CODE: {
            auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            auto* n = b.open(NodeType::CodeBlock);
            if (code) {
                n->data.text.assign(code->info.text, code->info.size);
                n->data.info.assign(code->lang.text, code->lang.size);
            }
            break;
        }
        case MD_BLOCK_HTML:
            b.open(NodeType::HtmlBlock);
            break;
        case MD_BLOCK_HR:
            b.leaf(NodeType::ThematicBreak);
            break;
        case MD_BLOCK_TABLE: {
            // Column alignment is derived on leave (MD_BLOCK_TABLE) once all
            // rows/cells are known.
            b.open(NodeType::Table);
            break;
        }
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            // Structural only — rows attach to the Table node directly.
            break;
        case MD_BLOCK_TR:
            b.open(NodeType::TableRow);
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            auto* td = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
            auto* n = b.open(NodeType::TableCell);
            n->data.header = (type == MD_BLOCK_TH);
            if (td) n->data.align = to_cell_align(td->align);
            break;
        }
        default:
            break;
    }
    return 0;
}

int on_leave_block(MD_BLOCKTYPE type, void*, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break; // never pushed
        case MD_BLOCK_TABLE: {
            // Derive per-column alignment from the first non-None cell align
            // in each column (md4c reports alignment per cell).
            ArenaNode* t = b.top();
            std::size_t ncols = 0;
            for (const ArenaNode* row : t->children)
                ncols = std::max(ncols, row->children.size());
            t->data.aligns.assign(ncols, CellAlign::None);
            for (std::size_t c = 0; c < ncols; ++c) {
                for (const ArenaNode* row : t->children) {
                    if (c < row->children.size() &&
                        row->children[c]->data.align != CellAlign::None) {
                        t->data.aligns[c] = row->children[c]->data.align;
                        break;
                    }
                }
            }
            b.close();
            break;
        }
        case MD_BLOCK_DOC:
            b.close(); // root
            break;
        default:
            b.close();
            break;
    }
    return 0;
}

int on_enter_span(MD_SPANTYPE type, void* detail, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_SPAN_EM: b.open(NodeType::Emphasis); break;
        case MD_SPAN_STRONG: b.open(NodeType::Strong); break;
        case MD_SPAN_DEL: b.open(NodeType::Del); break;
        case MD_SPAN_CODE: b.open(NodeType::CodeSpan); break;
        case MD_SPAN_A: {
            auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
            auto* n = b.open(NodeType::Link);
            if (a) {
                n->data.text.assign(a->href.text, a->href.size);
                n->data.title.assign(a->title.text, a->title.size);
            }
            break;
        }
        case MD_SPAN_IMG: {
            auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            auto* n = b.open(NodeType::Image);
            if (img) {
                n->data.text.assign(img->src.text, img->src.size);
                n->data.title.assign(img->title.text, img->title.size);
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

int on_leave_span(MD_SPANTYPE, void*, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    b.close();
    return 0;
}

int on_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    const std::string chunk(text, size);
    ArenaNode* top = b.top();
    switch (type) {
        case MD_TEXT_NORMAL:
            if (verbatim(top)) {
                (top->data.text).append(chunk);
            } else {
                auto* n = b.leaf(NodeType::Text);
                n->data.text = chunk;
            }
            break;
        case MD_TEXT_ENTITY: {
            const std::string decoded = decode_entity(chunk);
            if (verbatim(top)) {
                (top->data.text).append(decoded);
            } else {
                auto* n = b.leaf(NodeType::Text);
                n->data.text = decoded;
            }
            break;
        }
        case MD_TEXT_BR: b.leaf(NodeType::HardBreak); break;
        case MD_TEXT_SOFTBR: b.leaf(NodeType::SoftBreak); break;
        case MD_TEXT_HTML:
            if (verbatim(top)) {
                (top->data.text).append(chunk);
            } else {
                auto* n = b.leaf(NodeType::HtmlSpan);
                n->data.text = chunk;
            }
            break;
        default:
            // NULL chars / anything else: emit as replacement text for safety.
            if (!verbatim(top)) {
                auto* n = b.leaf(NodeType::Text);
                n->data.text = chunk.empty() ? "\xEF\xBF\xBD" : chunk;
            } else {
                (top->data.text).append(chunk);
            }
            break;
    }
    return 0;
}

// Turns a [[TOC]] paragraph into a Toc node.
void mark_toc_nodes(Builder& b) {
    if (!b.root) return;
    for (auto* child : b.root->children) {
        if (child->data.type != NodeType::Paragraph || child->children.size() != 1) continue;
        const ArenaNode* only = child->children.front();
        if (only->data.type != NodeType::Text) continue;
        std::string t = only->data.text;
        // Trim trailing/leading whitespace.
        auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        while (!t.empty() && is_space(t.front())) t.erase(t.begin());
        while (!t.empty() && is_space(t.back())) t.pop_back();
        if (t == "[[TOC]]") {
            child->data = Node{};
            child->data.type = NodeType::Toc;
            child->children.clear();
        }
    }
}

} // namespace

MarkdownAst MarkdownAst::parse(const std::string& source) {
    Builder b;
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = static_cast<unsigned>(
        MD_DIALECT_GITHUB); // permissive autolinks | tables | strikethrough | tasklists
    parser.enter_block = on_enter_block;
    parser.leave_block = on_leave_block;
    parser.enter_span = on_enter_span;
    parser.leave_span = on_leave_span;
    parser.text = on_text;
    md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &b);
    mark_toc_nodes(b);
    if (!b.root) {
        Node root;
        root.type = NodeType::Root;
        return MarkdownAst(std::move(root));
    }
    return MarkdownAst(b.to_value(b.root));
}

namespace {

void heading_plain(const Node& n, std::string& out) {
    switch (n.type) {
        case NodeType::Text:
        case NodeType::CodeSpan:
            out += n.text;
            return;
        case NodeType::SoftBreak:
        case NodeType::HardBreak:
            out += ' ';
            return;
        case NodeType::Image:
            out += n.title.empty() ? n.text : n.title;
            return;
        default:
            break;
    }
    for (const Node& c : n.children) heading_plain(c, out);
}

void collect_headings(const Node& n, std::vector<MarkdownAst::Heading>& out) {
    if (n.type == NodeType::Heading) {
        std::string text;
        for (const Node& c : n.children) heading_plain(c, text);
        // Normalize inner whitespace.
        std::string clean;
        bool pending_space = false;
        for (char ch : text) {
            if (ch == ' ' || ch == '\t' || ch == '\n') {
                pending_space = !clean.empty();
            } else {
                if (pending_space) clean += ' ';
                pending_space = false;
                clean += ch;
            }
        }
        out.push_back(MarkdownAst::Heading{n.level, clean, ""});
        return; // headings have no nested block children worth descending into
    }
    for (const Node& c : n.children) collect_headings(c, out);
}

} // namespace

std::vector<MarkdownAst::Heading> MarkdownAst::headings() const {
    std::vector<Heading> out;
    collect_headings(root_, out);
    std::map<std::string, int> counts;
    for (auto& h : out) {
        const std::string base = slugify_heading(h.text);
        int& used = counts[base];
        h.anchor = base;
        if (used > 0) h.anchor = base + "-" + std::to_string(used + 1);
        ++used;
    }
    return out;
}

} // namespace remin::markdown