#include "gui/markdown/markdown_style.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace remin::markdown {

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(const std::string& s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// ---- colors ---------------------------------------------------------------

bool parse_hex(const std::string& v, Color& out) {
    if (v.empty() || v[0] != '#') return false;
    const std::string h = v.substr(1);
    if (h.size() == 6) {
        unsigned val = 0;
        if (std::sscanf(h.c_str(), "%x", &val) != 1) return false;
        out.r = static_cast<float>((val >> 16) & 0xFF) / 255.0f;
        out.g = static_cast<float>((val >> 8) & 0xFF) / 255.0f;
        out.b = static_cast<float>(val & 0xFF) / 255.0f;
        out.a = 1.0f;
        out.valid = true;
        return true;
    }
    if (h.size() == 3) {
        unsigned v1 = 0, v2 = 0, v3 = 0;
        if (std::sscanf(h.c_str(), "%1x%1x%1x", &v1, &v2, &v3) != 3) return false;
        out.r = static_cast<float>((v1 << 4) | v1) / 255.0f;
        out.g = static_cast<float>((v2 << 4) | v2) / 255.0f;
        out.b = static_cast<float>((v3 << 4) | v3) / 255.0f;
        out.a = 1.0f;
        out.valid = true;
        return true;
    }
    return false;
}

bool parse_rgb(const std::string& v, Color& out) {
    if (v.rfind("rgb", 0) != 0) return false;
    const std::size_t open = v.find('(');
    const std::size_t close = v.find(')');
    if (open == std::string::npos || close == std::string::npos || close < open)
        return false;
    const std::string inside = v.substr(open + 1, close - open - 1);
    if (inside.rfind("a", 0) == 0) return false;
    float r = 0, g = 0, b = 0;
    if (std::sscanf(inside.c_str(), "%f,%f,%f", &r, &g, &b) != 3) return false;
    out.r = r / 255.0f;
    out.g = g / 255.0f;
    out.b = b / 255.0f;
    out.a = 1.0f;
    out.valid = true;
    return true;
}

Color named_color(const std::string& name) {
    const std::string n = lower(name);
    struct Entry { const char* name; float r, g, b; };
    static const Entry kNamed[] = {
        {"black", 0.0f, 0.0f, 0.0f},   {"white", 1.0f, 1.0f, 1.0f},
        {"red", 0.75f, 0.16f, 0.16f},  {"green", 0.13f, 0.55f, 0.26f},
        {"blue", 0.23f, 0.35f, 0.95f}, {"orange", 0.75f, 0.45f, 0.04f},
        {"gray", 0.42f, 0.44f, 0.48f}, {"grey", 0.42f, 0.44f, 0.48f},
        {"yellow", 0.9f, 0.75f, 0.15f},
    };
    for (const auto& c : kNamed)
        if (n == c.name) return Color{c.r, c.g, c.b, 1.0f, true};
    return {};
}

bool parse_color(const std::string& raw, const ReminPalette& palette, Color& out) {
    const std::string v = trim(lower(raw));
    if (v.empty()) return false;
    if (parse_hex(v, out)) return true;
    if (parse_rgb(v, out)) return true;
    if (v[0] == '@') {
        const std::string tok = v.substr(1);
        const Color* cand = nullptr;
        if (tok == "text") cand = &palette.text;
        else if (tok == "text-muted") cand = &palette.text_muted;
        else if (tok == "accent") cand = &palette.accent;
        else if (tok == "bg") cand = &palette.bg;
        else if (tok == "surface") cand = &palette.surface;
        else if (tok == "border") cand = &palette.border;
        else if (tok == "code-bg") cand = &palette.code_bg;
        else if (tok == "quote-bg") cand = &palette.quote_bg;
        else if (tok == "red") cand = &palette.error;
        else if (tok == "orange" || tok == "amber") cand = &palette.warning;
        else if (tok == "green") cand = &palette.ok;
        else if (tok == "blue") cand = &palette.info;
        if (cand) {
            out = *cand;
            return true;
        }
        return false;
    }
    const Color named = named_color(v);
    if (named.valid) {
        out = named;
        return true;
    }
    return false;
}

// ---- selectors ------------------------------------------------------------

std::optional<StyleSelector> selector_from_name(const std::string& raw) {
    const std::string s = lower(trim(raw));
    if (s == "*" || s == "document") return StyleSelector::Document;
    if (s == "h1") return StyleSelector::H1;
    if (s == "h2") return StyleSelector::H2;
    if (s == "h3") return StyleSelector::H3;
    if (s == "h4") return StyleSelector::H4;
    if (s == "h5") return StyleSelector::H5;
    if (s == "h6") return StyleSelector::H6;
    if (s == "p" || s == "paragraph") return StyleSelector::Paragraph;
    if (s == "code") return StyleSelector::Code;
    if (s == "pre" || s == "pre > code") return StyleSelector::Pre;
    if (s == "blockquote") return StyleSelector::Blockquote;
    if (s == "a" || s == "a:link" || s == "link") return StyleSelector::Link;
    if (s == "ul" || s == "ol" || s == "list") return StyleSelector::List;
    if (s == "li" || s == "list-item") return StyleSelector::ListItem;
    if (s == "table") return StyleSelector::Table;
    if (s == "th" || s == "thead th") return StyleSelector::TableHeader;
    if (s == "td" || s == "tbody td") return StyleSelector::TableCell;
    if (s == "tr") return StyleSelector::TableRow;
    if (s == "hr") return StyleSelector::Hr;
    if (s == ".toc") return StyleSelector::Toc;
    if (s == ".toc-title") return StyleSelector::TocTitle;
    if (s == ".toc-link") return StyleSelector::TocLink;
    if (s == "img" || s == "image") return StyleSelector::Image;
    return std::nullopt;
}

bool is_page_selector(const std::string& raw) {
    const std::string s = lower(trim(raw));
    return s == "page" || s == "@page";
}

} // namespace

double pt_from_unit(const std::string& value, double fallback) {
    const std::string v = trim(value);
    if (v.empty()) return fallback;
    char* end = nullptr;
    const double n = std::strtod(v.c_str(), &end);
    if (end == v.c_str()) return fallback;
    const std::string unit = lower(trim(std::string(end)));
    if (unit == "pt" || unit.empty()) return n;
    if (unit == "mm") return n * 2.83464566929134;
    if (unit == "cm") return n * 28.3464566929134;
    if (unit == "in") return n * 72.0;
    if (unit == "px") return n * 0.75;
    return fallback;
}

// ---------------------------------------------------------------------------
// default styles
// ---------------------------------------------------------------------------

StyleSheet default_style(const ReminPalette& p) {
    StyleSheet s;
    s.base_font = "sans";
    s.base_font_pt = 11.0;
    s.line_height = 1.55;
    s.text_color = p.text;
    s.bg_color = p.bg;
    s.hr_color = p.border;
    s.link_color = p.accent;

    auto h_style = [](double size, int wt) {
        TextStyle t;
        t.font_size_pt = size;
        t.weight = wt;
        return t;
    };
    auto h_box = [](double mt, double mb) {
        BoxStyle b;
        b.margin_top_pt = mt;
        b.margin_bottom_pt = mb;
        b.any_set = true;
        return b;
    };

    s.text[static_cast<int>(StyleSelector::H1)] = h_style(21.0, 700);
    s.text[static_cast<int>(StyleSelector::H2)] = h_style(16.5, 700);
    s.text[static_cast<int>(StyleSelector::H3)] = h_style(13.5, 700);
    s.text[static_cast<int>(StyleSelector::H4)] = h_style(12.0, 700);
    s.text[static_cast<int>(StyleSelector::H5)] = h_style(11.0, 700);
    s.text[static_cast<int>(StyleSelector::H6)] = h_style(11.0, 700);
    s.box[static_cast<int>(StyleSelector::H1)] = h_box(18.0, 9.0);
    s.box[static_cast<int>(StyleSelector::H2)] = h_box(14.0, 7.0);
    s.box[static_cast<int>(StyleSelector::H3)] = h_box(12.0, 5.0);
    s.box[static_cast<int>(StyleSelector::H4)] = h_box(10.0, 4.0);
    s.box[static_cast<int>(StyleSelector::H5)] = h_box(9.0, 3.0);
    s.box[static_cast<int>(StyleSelector::H6)] = h_box(9.0, 3.0);

    TextStyle code;
    code.font_family = "monospace";
    code.font_size_pt = 10.0;
    code.background = p.code_bg;
    s.text[static_cast<int>(StyleSelector::Code)] = code;

    TextStyle pre;
    pre.font_family = "monospace";
    pre.font_size_pt = 10.0;
    pre.background = p.surface;
    s.text[static_cast<int>(StyleSelector::Pre)] = pre;
    BoxStyle pre_box;
    pre_box.margin_top_pt = 8.0;
    pre_box.margin_bottom_pt = 8.0;
    pre_box.padding_pt = 8.0;
    pre_box.border_width_pt = 1.0;
    pre_box.border_color = p.border;
    pre_box.any_set = true;
    s.box[static_cast<int>(StyleSelector::Pre)] = pre_box;

    TextStyle quote;
    quote.font_size_pt = 11.0;
    quote.background = p.quote_bg;
    s.text[static_cast<int>(StyleSelector::Blockquote)] = quote;
    BoxStyle quote_box;
    quote_box.margin_top_pt = 6.0;
    quote_box.margin_bottom_pt = 6.0;
    quote_box.padding_pt = 8.0;
    quote_box.border_left_width_pt = 3.0;
    quote_box.border_color = p.border;
    quote_box.any_set = true;
    s.box[static_cast<int>(StyleSelector::Blockquote)] = quote_box;

    TextStyle link;
    link.color = p.accent;
    link.underline = true;
    s.text[static_cast<int>(StyleSelector::Link)] = link;

    BoxStyle para;
    para.margin_top_pt = 4.0;
    para.margin_bottom_pt = 4.0;
    para.any_set = true;
    s.box[static_cast<int>(StyleSelector::Paragraph)] = para;

    BoxStyle li;
    li.margin_top_pt = 1.0;
    li.margin_bottom_pt = 1.0;
    li.any_set = true;
    s.box[static_cast<int>(StyleSelector::ListItem)] = li;

    BoxStyle list;
    list.margin_top_pt = 4.0;
    list.margin_bottom_pt = 4.0;
    list.any_set = true;
    s.box[static_cast<int>(StyleSelector::List)] = list;

    s.text[static_cast<int>(StyleSelector::TableHeader)].weight = 700;
    s.text[static_cast<int>(StyleSelector::TableHeader)].background = p.surface;
    BoxStyle th;
    th.padding_pt = 3.0;
    th.any_set = true;
    s.box[static_cast<int>(StyleSelector::TableHeader)] = th;
    BoxStyle td;
    td.padding_pt = 3.0;
td.any_set = true;
    s.box[static_cast<int>(StyleSelector::TableCell)] = td;
    BoxStyle tbl;
    tbl.margin_top_pt = 8.0;
    tbl.margin_bottom_pt = 8.0;
    tbl.border_width_pt = 1.0;
    tbl.border_color = p.border;
    tbl.any_set = true;
    s.box[static_cast<int>(StyleSelector::Table)] = tbl;

    TextStyle toc_title;
    toc_title.weight = 700;
    s.text[static_cast<int>(StyleSelector::TocTitle)] = toc_title;
    TextStyle toc_link;
    toc_link.color = p.text;
    s.text[static_cast<int>(StyleSelector::TocLink)] = toc_link;
    BoxStyle toc_box;
    toc_box.margin_top_pt = 6.0;
    toc_box.margin_bottom_pt = 12.0;
    toc_box.any_set = true;
    s.box[static_cast<int>(StyleSelector::Toc)] = toc_box;

    return s;
}

ReminPalette light_palette() {
    ReminPalette p;
    p.text = Color{0.13f, 0.15f, 0.17f, 1.0f, true};
    p.text_muted = Color{0.42f, 0.44f, 0.48f, 1.0f, true};
    p.accent = Color{0.25f, 0.34f, 0.92f, 1.0f, true};
    p.bg = Color{1.0f, 1.0f, 1.0f, 1.0f, true};
    p.surface = Color{0.97f, 0.98f, 0.98f, 1.0f, true};
    p.border = Color{0.80f, 0.83f, 0.88f, 1.0f, true};
    p.code_bg = Color{0.94f, 0.94f, 0.96f, 1.0f, true};
    p.quote_bg = Color{0.96f, 0.97f, 0.99f, 1.0f, true};
    return p;
}

ReminPalette dark_palette() {
    ReminPalette p;
    p.text = Color{0.91f, 0.92f, 0.94f, 1.0f, true};
    p.text_muted = Color{0.62f, 0.64f, 0.68f, 1.0f, true};
    p.accent = Color{0.56f, 0.62f, 0.98f, 1.0f, true};
    p.bg = Color{0.10f, 0.11f, 0.13f, 1.0f, true};
    p.surface = Color{0.15f, 0.16f, 0.18f, 1.0f, true};
    p.border = Color{0.28f, 0.30f, 0.34f, 1.0f, true};
    p.code_bg = Color{0.17f, 0.18f, 0.20f, 1.0f, true};
    p.quote_bg = Color{0.14f, 0.15f, 0.17f, 1.0f, true};
    return p;
}

// ---------------------------------------------------------------------------
// CSS-like subset parser
// ---------------------------------------------------------------------------

namespace {

void set_box_value(const std::string& name, const std::string& val, BoxStyle& b) {
    const std::string n = lower(name);
    const double v = pt_from_unit(val, 0.0);
    if (n == "margin") {
        b.margin_top_pt = b.margin_bottom_pt = b.margin_left_pt = b.margin_right_pt = v;
    } else if (n == "margin-top") {
        b.margin_top_pt = v;
    } else if (n == "margin-bottom") {
        b.margin_bottom_pt = v;
    } else if (n == "margin-left") {
        b.margin_left_pt = v;
    } else if (n == "margin-right") {
        b.margin_right_pt = v;
    } else if (n == "padding") {
        b.padding_pt = v;
    } else if (n == "border") {
        b.border_width_pt = v;
        b.border_left_width_pt = v > 0.0 ? v : b.border_left_width_pt;
    } else if (n == "border-left" || n == "border-left-width") {
        b.border_left_width_pt = v;
    } else if (n == "border-width") {
        b.border_width_pt = v;
    }
    b.any_set = true;
}

void parse_declarations(const std::string& body, StyleSheet& s,
                        const ReminPalette& palette, StyleSelector sel,
                        bool is_page) {
    if (is_page) {
        // page geometry: handled from the *document-level* rule too.
        return;
    }
    TextStyle& t = s.text[static_cast<int>(sel)];
    BoxStyle& b = s.box[static_cast<int>(sel)];
    StyleSheet* doc = &s;

    std::size_t i = 0;
    while (i < body.size()) {
        std::size_t semi = body.find(';', i);
        if (semi == std::string::npos) semi = body.size();
        const std::string decl = body.substr(i, semi - i);
        i = semi + 1;
        const std::size_t colon = decl.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(trim(decl.substr(0, colon)));
        const std::string value = trim(decl.substr(colon + 1));
        if (name.empty() || value.empty()) continue;

        if (sel == StyleSelector::Document) {
            if (name == "color") { if (parse_color(value, palette, doc->text_color)) continue; }
            if (name == "background" || name == "background-color") {
                if (parse_color(value, palette, doc->bg_color)) continue;
            }
            if (name == "font-family") { doc->base_font = value; continue; }
            if (name == "font-size") { doc->base_font_pt = pt_from_unit(value, doc->base_font_pt); continue; }
            if (name == "line-height") {
                double lh = pt_from_unit(value, -1.0);
                if (lh > 0.0) { doc->line_height = std::max(1.0, lh); continue; }
                const std::string lv = trim(value);
                if (!lv.empty() && std::isdigit(static_cast<unsigned char>(lv[0]))) {
                    char* end = nullptr;
                    double n = std::strtod(lv.c_str(), &end);
                    if (end != lv.c_str() && n > 0.0) { doc->line_height = n; }
                    continue;
                }
                continue;
            }
        }

        if (name == "color") { if (parse_color(value, palette, t.color)) {} }
        else if (name == "background" || name == "background-color") { if (parse_color(value, palette, t.background)) {} }
        else if (name == "font-family") { t.font_family = value; }
        else if (name == "font-size") { t.font_size_pt = pt_from_unit(value, t.font_size_pt); }
        else if (name == "font-weight") {
            const std::string lv = lower(value);
            if (lv == "bold") t.weight = 700;
            else if (lv == "normal") t.weight = 400;
            else { char* end = nullptr; long n = std::strtol(lv.c_str(), &end, 10); if (end != lv.c_str()) t.weight = static_cast<int>(n); }
        }
        else if (name == "font-style") { t.italic = lower(value) == "italic"; t.italic_set = true; }
        else if (name == "text-decoration") {
            const std::string lv = lower(value);
            if (lv == "underline") { t.underline = true; t.underline_set = true; }
            else if (lv == "strikethrough" || lv == "line-through") { t.strike = true; t.strike_set = true; }
            else if (lv == "none") { t.underline = t.strike = false; t.underline_set = t.strike_set = true; }
        }
        else {
            set_box_value(name, value, b);
            // hr / image also accept color via box path? no; done above.
        }
        (void)doc;
    }
}

void apply_page_rule(const std::string& body, StyleSheet& s) {
    std::size_t i = 0;
    while (i < body.size()) {
        std::size_t semi = body.find(';', i);
        if (semi == std::string::npos) semi = body.size();
        const std::string decl = body.substr(i, semi - i);
        i = semi + 1;
        const std::size_t colon = decl.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(trim(decl.substr(0, colon)));
        const std::string value = trim(decl.substr(colon + 1));
        if (name.empty() || value.empty()) continue;
        if (name == "size") {
            const std::string v = lower(value);
            if (v == "a4") { s.page_width_pt = 595.28; s.page_height_pt = 841.89; s.page_size = "a4"; }
            else if (v == "letter") { s.page_width_pt = 612.0; s.page_height_pt = 792.0; s.page_size = "letter"; }
        }
        else if (name == "width") s.page_width_pt = pt_from_unit(value, s.page_width_pt);
        else if (name == "height") s.page_height_pt = pt_from_unit(value, s.page_height_pt);
        else if (name == "margin") s.page_margin_pt = pt_from_unit(value, s.page_margin_pt);
    }
}

} // namespace

StyleSheet apply_style(const std::string& css, const StyleSheet& base,
                       const ReminPalette& palette) {
    StyleSheet s = base;
    std::size_t i = 0;
    while (i < css.size()) {
        // Find next '{' and matching '}'.
        const std::size_t open = css.find('{', i);
        if (open == std::string::npos) break;
        const std::size_t close = css.find('}', open + 1);
        if (close == std::string::npos) break;
        const std::string selector_group = css.substr(i, open - i);
        const std::string body = css.substr(open + 1, close - open - 1);
        i = close + 1;

        bool any_page = false;
        bool any_mapped = false;
        std::size_t pos = 0;
        while (pos <= selector_group.size()) {
            const std::size_t comma = selector_group.find(',', pos);
            const std::string part = selector_group.substr(
                pos, comma == std::string::npos ? std::string::npos : comma - pos);
            const std::string sel = trim(part);
            if (is_page_selector(sel)) apply_page_rule(body, s);
            else if (auto mapped = selector_from_name(sel)) {
                parse_declarations(body, s, palette, *mapped, false);
                any_mapped = true;
            } else {
                any_mapped = true;
            }
            if (is_page_selector(sel)) any_page = true;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        (void)any_page;
        (void)any_mapped;
    }
    return s;
}

// ---------------------------------------------------------------------------
// resolved accessors
// ---------------------------------------------------------------------------

std::string StyleSheet::font_for(StyleSelector s) const {
    const TextStyle& t = text[static_cast<int>(s)];
    return t.font_family.empty() ? base_font : t.font_family;
}
double StyleSheet::font_size_for(StyleSelector s) const {
    const TextStyle& t = text[static_cast<int>(s)];
    return t.font_size_pt > 0.0 ? t.font_size_pt : base_font_pt;
}
Color StyleSheet::color_for(StyleSelector s) const {
    const TextStyle& t = text[static_cast<int>(s)];
    if (t.color.valid) return t.color;
    return s == StyleSelector::Link ? link_color : text_color;
}
bool StyleSheet::italic_for(StyleSelector s) const {
    const TextStyle& t = text[static_cast<int>(s)];
    return t.italic_set ? t.italic : false;
}
bool StyleSheet::strike_for(StyleSelector s) const {
    const TextStyle& t = text[static_cast<int>(s)];
    return t.strike_set ? t.strike : false;
}
bool StyleSheet::underline_for(StyleSelector s) const {
    const TextStyle& t = text[static_cast<int>(s)];
    if (t.underline_set) return t.underline;
    return s == StyleSelector::Link;
}
Color StyleSheet::bg_color_for(StyleSelector s) const {
    return text[static_cast<int>(s)].background;
}
BoxStyle StyleSheet::box_for(StyleSelector s) const {
    return box[static_cast<int>(s)];
}

} // namespace remin::markdown