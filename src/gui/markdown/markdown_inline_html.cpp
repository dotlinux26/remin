#include "gui/markdown/markdown_inline_html.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>

namespace remin::markdown {

namespace {

// ---- whitespace / case helpers --------------------------------------------

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && is_space(s[a])) ++a;
    while (b > a && is_space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

// ---- named colours (standard CSS keyword table) ---------------------------

const std::map<std::string, std::pair<int, int>>& named_colors() {
    static const std::map<std::string, std::pair<int, int>> table = {
        {"aliceblue", {0xf0f8ff, 0}},  {"antiquewhite", {0xfaebd7, 0}},
        {"aqua", {0x00ffff, 0}},       {"aquamarine", {0x7fffd4, 0}},
        {"azure", {0xf0ffff, 0}},      {"beige", {0xf5f5dc, 0}},
        {"bisque", {0xffe4c4, 0}},     {"black", {0x000000, 0}},
        {"blanchedalmond", {0xffebcd, 0}}, {"blue", {0x0000ff, 0}},
        {"blueviolet", {0x8a2be2, 0}}, {"brown", {0xa52a2a, 0}},
        {"burlywood", {0xdeb887, 0}},  {"cadetblue", {0x5f9ea0, 0}},
        {"chartreuse", {0x7fff00, 0}}, {"chocolate", {0xd2691e, 0}},
        {"coral", {0xff7f50, 0}},      {"cornflowerblue", {0x6495ed, 0}},
        {"cornsilk", {0xfff8dc, 0}},   {"crimson", {0xdc143c, 0}},
        {"cyan", {0x00ffff, 0}},       {"darkblue", {0x00008b, 0}},
        {"darkcyan", {0x008b8b, 0}},   {"darkgoldenrod", {0xb8860b, 0}},
        {"darkgray", {0xa9a9a9, 0}},   {"darkgreen", {0x006400, 0}},
        {"darkgrey", {0xa9a9a9, 0}},   {"darkkhaki", {0xbdb76b, 0}},
        {"darkmagenta", {0x8b008b, 0}}, {"darkolivegreen", {0x556b2f, 0}},
        {"darkorange", {0xff8c00, 0}}, {"darkorchid", {0x9932cc, 0}},
        {"darkred", {0x8b0000, 0}},    {"darksalmon", {0xe9967a, 0}},
        {"darkseagreen", {0x8fbc8f, 0}}, {"darkslateblue", {0x483d8b, 0}},
        {"darkslategray", {0x2f4f4f, 0}}, {"darkslategrey", {0x2f4f4f, 0}},
        {"darkturquoise", {0x00ced1, 0}}, {"darkviolet", {0x9400d3, 0}},
        {"deeppink", {0xff1493, 0}},   {"deepskyblue", {0x00bfff, 0}},
        {"dimgray", {0x696969, 0}},    {"dimgrey", {0x696969, 0}},
        {"dodgerblue", {0x1e90ff, 0}}, {"firebrick", {0xb22222, 0}},
        {"floralwhite", {0xfffaf0, 0}}, {"forestgreen", {0x228b22, 0}},
        {"fuchsia", {0xff00ff, 0}},    {"gainsboro", {0xdcdcdc, 0}},
        {"ghostwhite", {0xf8f8ff, 0}}, {"gold", {0xffd700, 0}},
        {"goldenrod", {0xdaa520, 0}},  {"gray", {0x808080, 0}},
        {"green", {0x008000, 0}},      {"greenyellow", {0xadff2f, 0}},
        {"grey", {0x808080, 0}},       {"honeydew", {0xf0fff0, 0}},
        {"hotpink", {0xff69b4, 0}},    {"indianred", {0xcd5c5c, 0}},
        {"indigo", {0x4b0082, 0}},     {"ivory", {0xfffff0, 0}},
        {"khaki", {0xf0e68c, 0}},      {"lavender", {0xe6e6fa, 0}},
        {"lavenderblush", {0xfff0f5, 0}}, {"lawngreen", {0x7cfc00, 0}},
        {"lemonchiffon", {0xfffacd, 0}}, {"lightblue", {0xadd8e6, 0}},
        {"lightcoral", {0xf08080, 0}}, {"lightcyan", {0xe0ffff, 0}},
        {"lightgoldenrodyellow", {0xfafad2, 0}}, {"lightgray", {0xd3d3d3, 0}},
        {"lightgreen", {0x90ee90, 0}}, {"lightgrey", {0xd3d3d3, 0}},
        {"lightpink", {0xffb6c1, 0}},  {"lightsalmon", {0xffa07a, 0}},
        {"lightseagreen", {0x20b2aa, 0}}, {"lightskyblue", {0x87cefa, 0}},
        {"lightslategray", {0x778899, 0}}, {"lightslategrey", {0x778899, 0}},
        {"lightsteelblue", {0xb0c4de, 0}}, {"lightyellow", {0xffffe0, 0}},
        {"lime", {0x00ff00, 0}},       {"limegreen", {0x32cd32, 0}},
        {"linen", {0xfaf0e6, 0}},      {"magenta", {0xff00ff, 0}},
        {"maroon", {0x800000, 0}},     {"mediumaquamarine", {0x66cdaa, 0}},
        {"mediumblue", {0x0000cd, 0}}, {"mediumorchid", {0xba55d3, 0}},
        {"mediumpurple", {0x9370db, 0}}, {"mediumseagreen", {0x3cb371, 0}},
        {"mediumslateblue", {0x7b68ee, 0}}, {"mediumspringgreen", {0x00fa9a, 0}},
        {"mediumturquoise", {0x48d1cc, 0}}, {"mediumvioletred", {0xc71585, 0}},
        {"midnightblue", {0x191970, 0}}, {"mintcream", {0xf5fffa, 0}},
        {"mistyrose", {0xffe4e1, 0}},  {"moccasin", {0xffe4b5, 0}},
        {"navajowhite", {0xffdead, 0}}, {"navy", {0x000080, 0}},
        {"oldlace", {0xfdf5e6, 0}},    {"olive", {0x808000, 0}},
        {"olivedrab", {0x6b8e23, 0}},  {"orange", {0xffa500, 0}},
        {"orangered", {0xff4500, 0}},  {"orchid", {0xda70d6, 0}},
        {"palegoldenrod", {0xeee8aa, 0}}, {"palegreen", {0x98fb98, 0}},
        {"paleturquoise", {0xafeeee, 0}}, {"palevioletred", {0xdb7093, 0}},
        {"papayawhip", {0xffefd5, 0}}, {"peachpuff", {0xffdab9, 0}},
        {"peru", {0xcd853f, 0}},       {"pink", {0xffc0cb, 0}},
        {"plum", {0xdda0dd, 0}},       {"powderblue", {0xb0e0e6, 0}},
        {"purple", {0x800080, 0}},     {"rebeccapurple", {0x663399, 0}},
        {"red", {0xff0000, 0}},        {"rosybrown", {0xbc8f8f, 0}},
        {"royalblue", {0x4169e1, 0}},  {"saddlebrown", {0x8b4513, 0}},
        {"salmon", {0xfa8072, 0}},     {"sandybrown", {0xf4a460, 0}},
        {"seagreen", {0x2e8b57, 0}},   {"seashell", {0xfff5ee, 0}},
        {"sienna", {0xa0522d, 0}},     {"silver", {0xc0c0c0, 0}},
        {"skyblue", {0x87ceeb, 0}},    {"slateblue", {0x6a5acd, 0}},
        {"slategray", {0x708090, 0}},  {"slategrey", {0x708090, 0}},
        {"snow", {0xfffafa, 0}},       {"springgreen", {0x00ff7f, 0}},
        {"steelblue", {0x4682b4, 0}},  {"tan", {0xd2b48c, 0}},
        {"teal", {0x008080, 0}},       {"thistle", {0xd8bfd8, 0}},
        {"tomato", {0xff6347, 0}},     {"turquoise", {0x40e0d0, 0}},
        {"violet", {0xee82ee, 0}},     {"wheat", {0xf5deb3, 0}},
        {"white", {0xffffff, 0}},      {"whitesmoke", {0xf5f5f5, 0}},
        {"yellow", {0xffff00, 0}},     {"yellowgreen", {0x9acd32, 0}},
    };
    return table;
}

Color color_from_rgb(int r, int g, int b, float a) {
    Color c;
    c.r = static_cast<float>(r) / 255.0f;
    c.g = static_cast<float>(g) / 255.0f;
    c.b = static_cast<float>(b) / 255.0f;
    c.a = a;
    c.valid = true;
    return c;
}

// Parse one <number> or <percentage> channel of rgb()/rgba() (0-255).
bool parse_channel(const std::string& s, int& out) {
    std::string v = trim(s);
    if (v.empty()) return false;
    if (v.back() == '%') {
        v.pop_back();
        char* end = nullptr;
        const double pct = std::strtod(v.c_str(), &end);
        if (!end || *end != '\0') return false;
        out = static_cast<int>(std::clamp(pct, 0.0, 100.0) * 255.0 / 100.0);
        return true;
    }
    char* end = nullptr;
    const double num = std::strtod(v.c_str(), &end);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(std::clamp(num, 0.0, 255.0));
    return true;
}

bool parse_alpha_channel(const std::string& s, float& out) {
    std::string v = trim(s);
    if (empty(v)) return false;
    if (v.back() == '%') {
        v.pop_back();
        char* end = nullptr;
        const double pct = std::strtod(v.c_str(), &end);
        if (!end || *end != '\0') return false;
        out = static_cast<float>(std::clamp(pct, 0.0, 100.0) / 100.0);
        return true;
    }
    char* end = nullptr;
    const double num = std::strtod(v.c_str(), &end);
    if (!end || *end != '\0') return false;
    out = static_cast<float>(std::clamp(num, 0.0, 1.0));
    return true;
}

std::string strip_function(const std::string& v, const std::string& fn) {
    if (to_lower(v).rfind(fn, 0) != 0) return {};
    const std::size_t o = v.find('(');
    if (o == std::string::npos) return {};
    const std::string inner = v.substr(o + 1);
    const auto close = inner.find_last_of(')');
    if (close == std::string::npos) return {};
    return inner.substr(0, close);
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

} // namespace

// ---- color parsing ---------------------------------------------------------

Color parse_css_color(const std::string& value) {
    const std::string v = to_lower(trim(value));
    if (v.empty()) return {};

    if (const auto it = named_colors().find(v); it != named_colors().end()) {
        const int rgb = it->second.first;
        return color_from_rgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 1.0f);
    }

    if (v[0] == '#') {
        std::string h = v.substr(1);
        if (h.size() == 3 || h.size() == 4) {
            std::string ex;
            ex.reserve(h.size() * 2);
            for (char c : h) {
                if (hex_digit(c) < 0) return {};
                ex += c;
                ex += c;
            }
            h = ex;
        }
        if (h.size() != 6 && h.size() != 8) return {};
        int nib[8];
        for (std::size_t i = 0; i < h.size(); ++i) {
            nib[i] = hex_digit(h[i]);
            if (nib[i] < 0) return {};
        }
        const int r = (nib[0] << 4) | nib[1];
        const int g = (nib[2] << 4) | nib[3];
        const int b = (nib[4] << 4) | nib[5];
        if (h.size() == 6) return color_from_rgb(r, g, b, 1.0f);
        const int a = (nib[6] << 4) | nib[7];
        return color_from_rgb(r, g, b, static_cast<float>(a) / 255.0f);
    }

    std::string inner = strip_function(v, "rgba");
    const bool has_alpha = !inner.empty();
    if (!has_alpha) inner = strip_function(v, "rgb");
    if (inner.empty()) return {};

    std::vector<std::string> parts;
    std::string cur;
    for (char c : inner) {
        if (c == ',' || c == '/') {
            parts.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(trim(cur));
    const std::size_t need = has_alpha ? 4 : 3;
    if (parts.size() != need) return {};
    int ch[3];
    for (std::size_t k = 0; k < 3; ++k)
        if (!parse_channel(parts[k], ch[k])) return {};
    float a = 1.0f;
    if (has_alpha && !parse_alpha_channel(parts[3], a)) return {};
    return color_from_rgb(ch[0], ch[1], ch[2], a);
}

// ---- CSS declaration parsing ------------------------------------------------

InlineCssStyle parse_css_declaration(const std::string& css) {
    InlineCssStyle out;
    std::size_t i = 0;
    while (i <= css.size()) {
        const std::size_t semi = css.find(';', i);
        const std::string decl = css.substr(i, semi == std::string::npos ? css.size() - i : semi - i);
        i = semi == std::string::npos ? css.size() + 1 : semi + 1;
        const std::size_t colon = decl.find(':');
        if (colon == std::string::npos) continue;
        const std::string prop = to_lower(trim(decl.substr(0, colon)));
        const std::string value = trim(decl.substr(colon + 1));
        if (prop.empty() || value.empty()) continue;

        if (prop == "color") {
            const Color c = parse_css_color(value);
            if (c.valid) {
                out.has_color = true;
                out.color = c;
            }
        } else if (prop == "background-color") {
            const Color c = parse_css_color(value);
            if (c.valid) {
                out.has_bg = true;
                out.background = c;
            }
        } else if (prop == "font-weight") {
            const std::string wv = to_lower(value);
            if (wv == "bold" || wv == "bolder") {
                out.weight = 700;
            } else if (wv == "normal" || wv == "lighter") {
                out.weight = 400;
            } else {
                char* end = nullptr;
                const long w = std::strtol(wv.c_str(), &end, 10);
                if (end != wv.c_str() && end && *end == '\0' && w >= 100 && w <= 900)
                    out.weight = static_cast<int>(w);
            }
        } else if (prop == "font-style") {
            const std::string sv = to_lower(value);
            out.italic_set = true;
            out.italic = sv == "italic" || sv == "oblique";
        } else if (prop == "text-decoration") {
            const std::string tv = to_lower(value);
            if (tv.find("underline") != std::string::npos) {
                out.underline_set = true;
                out.underline = true;
            }
            if (tv.find("line-through") != std::string::npos) {
                out.strike_set = true;
                out.strike = true;
            }
            if (tv == "none") {
                out.underline_set = true;
                out.underline = false;
                out.strike_set = true;
                out.strike = false;
            }
        } else if (prop == "font-size") {
            if (!value.empty() && value.back() == '%') {
                const std::string num = value.substr(0, value.size() - 1);
                char* end = nullptr;
                const double pct = std::strtod(num.c_str(), &end);
                if (end != num.c_str() && end && *end == '\0')
                    out.size_pct = std::clamp(pct / 100.0, 0.05, 4.0);
            } else {
                char* end = nullptr;
                const double num = std::strtod(value.c_str(), &end);
                if (end != value.c_str() && end) {
                    const std::string unit = to_lower(trim(std::string(end)));
                    if ((unit == "pt" || unit.empty()) && num > 0.0)
                        out.size_pt = num;
                    else if (unit == "px" && num > 0.0)
                        out.size_pt = num * 0.75;
                }
            }
        } else if (prop == "font-family") {
            std::string f = value;
            if (f.size() >= 2 &&
                ((f.front() == '"' && f.back() == '"') ||
                 (f.front() == '\'' && f.back() == '\'')))
                f = f.substr(1, f.size() - 2);
            out.font_family = f;
        }
    }
    return out;
}

// ---- tag semantic defaults merged with inline CSS ---------------------------

InlineCssStyle style_from_tag(const std::string& name, const std::string& css,
                              const StyleSheet& style) {
    InlineCssStyle out;
    if (name == "b" || name == "strong") {
        out.weight = 700;
    } else if (name == "i" || name == "em") {
        out.italic_set = true;
        out.italic = true;
    } else if (name == "u") {
        out.underline_set = true;
        out.underline = true;
    } else if (name == "s" || name == "del") {
        out.strike_set = true;
        out.strike = true;
    } else if (name == "code") {
        out.font_family = "monospace";
        const Color cbg = style.text[static_cast<int>(StyleSelector::Code)].background;
        if (cbg.valid) {
            out.has_bg = true;
            out.background = cbg;
        }
        const double fp = style.text[static_cast<int>(StyleSelector::Code)].font_size_pt;
        if (fp > 0.0) out.size_pt = fp;
    } else if (name == "mark") {
        out.has_bg = true;
        out.background = color_from_rgb(0xff, 0xf3, 0xbf, 1.0f);
    }

    const InlineCssStyle css_style = parse_css_declaration(css);
    if (css_style.has_color) {
        out.has_color = true;
        out.color = css_style.color;
    }
    if (css_style.has_bg) {
        out.has_bg = true;
        out.background = css_style.background;
    }
    if (css_style.weight > 0) out.weight = css_style.weight;
    if (css_style.italic_set) {
        out.italic_set = true;
        out.italic = css_style.italic;
    }
    if (css_style.underline_set) {
        out.underline_set = true;
        out.underline = css_style.underline;
    }
    if (css_style.strike_set) {
        out.strike_set = true;
        out.strike = css_style.strike;
    }
    if (css_style.size_pt > 0.0) {
        out.size_pt = css_style.size_pt;
    } else if (css_style.size_pct > 0.0) {
        out.size_pt = 0.0;
        out.size_pct = css_style.size_pct;
    }
    if (!css_style.font_family.empty()) out.font_family = css_style.font_family;
    return out;
}

// ---- HTML tag tokenizer ------------------------------------------------------

std::optional<HtmlTag> parse_html_tag(const std::string& raw) {
    if (raw.size() < 2 || raw[0] != '<') return std::nullopt;
    if (raw[1] == '!' || raw[1] == '?') return std::nullopt;

    HtmlTag tag;
    std::size_t i = 1;
    if (raw[i] == '/') {
        tag.closing = true;
        ++i;
    }
    const std::size_t name_start = i;
    while (i < raw.size() && is_name_char(raw[i])) ++i;
    if (i == name_start) return std::nullopt;
    tag.name = to_lower(raw.substr(name_start, i - name_start));

    // Scan to '>' skipping quoted attribute values; detect trailing '/'.
    std::size_t gt = i;
    char quote = 0;
    bool self_close = false;
    for (; gt < raw.size(); ++gt) {
        const char c = raw[gt];
        if (quote != 0) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '/' && gt + 1 < raw.size() && raw[gt + 1] == '>') {
            self_close = true;
        } else if (c == '>') {
            break;
        }
    }
    if (gt >= raw.size()) return std::nullopt;  // unterminated

    // Attributes between name end and '>'.
    const std::string attrs = raw.substr(i, gt - i);
    std::size_t a = 0;
    while (a < attrs.size()) {
        while (a < attrs.size() && is_space(attrs[a])) ++a;
        if (a >= attrs.size()) break;
        const std::size_t nb = a;
        while (a < attrs.size() && !is_space(attrs[a]) && attrs[a] != '=') ++a;
        const std::string attr_name = to_lower(attrs.substr(nb, a - nb));
        if (!attr_name.empty()) {
            while (a < attrs.size() && is_space(attrs[a])) ++a;
            std::string value;
            if (a < attrs.size() && attrs[a] == '=') {
                ++a;
                while (a < attrs.size() && is_space(attrs[a])) ++a;
                if (a < attrs.size() && (attrs[a] == '"' || attrs[a] == '\'')) {
                    const char q = attrs[a];
                    const std::size_t vb = ++a;
                    while (a < attrs.size() && attrs[a] != q) ++a;
                    value = attrs.substr(vb, a - vb);
                    if (a < attrs.size()) ++a;
                } else {
                    const std::size_t vb = a;
                    while (a < attrs.size() && !is_space(attrs[a])) ++a;
                    value = attrs.substr(vb, a - vb);
                }
            }
            if (attr_name == "style") {
                tag.style = value;
                break;
            }
        }
    }

    tag.self_closing = self_close;
    tag.consumed = gt + 1;
    return tag;
}

bool is_block_level_tag(const std::string& name) {
    const std::string n = to_lower(name);
    static const char* kBlock[] = {
        "div", "section", "article", "aside", "header", "footer", "nav", "main",
        "p", "h1", "h2", "h3", "h4", "h5", "h6",
        "ul", "ol", "li", "dl", "dt", "dd",
        "table", "thead", "tbody", "tr", "td", "th",
        "blockquote", "pre", "hr", "form", "figure", "figcaption",
    };
    for (const char* b : kBlock)
        if (n == b) return true;
    return false;
}

// ---- whole-chunk renderer -----------------------------------------------------

bool render_html_block_runs(const std::string& html,
                            const StyleSheet& style,
                            const std::string& base_font,
                            double base_size_pt, Color base_color,
                            std::vector<StyledText>& out) {
    const std::string s = trim(html);
    if (s.empty()) return false;

    struct Scope {
        std::string name;
        std::string font;
        double size = 0.0;
        int weight = 400;
        bool italic = false, strike = false, underline = false;
        Color color;
        Color bg;
    };

    std::vector<StyledText> runs;
    std::vector<Scope> stack;

    std::string font = base_font;
    double size = base_size_pt;
    int weight = 400;
    bool italic = false, strike = false, underline = false;
    Color color = base_color;
    Color bg;

    bool text_pushed = false;
    bool tag_parsed = false;
    bool wrapper_open = false;
    std::string wrapper_name;
    bool in_list = false;
    bool in_li = false;
    bool first_li = true;
    int ol_count = 1;

    const auto apply = [&](const InlineCssStyle& cs) {
        if (cs.has_color) color = cs.color;
        if (cs.has_bg) bg = cs.background;
        if (cs.weight > 0) weight = cs.weight;
        if (cs.italic_set) italic = cs.italic;
        if (cs.underline_set) underline = cs.underline;
        if (cs.strike_set) strike = cs.strike;
        if (cs.size_pt > 0.0) size = cs.size_pt;
        else if (cs.size_pct > 0.0) size *= cs.size_pct;
        if (!cs.font_family.empty()) font = cs.font_family;
    };

    // Whitespace runs collapse to a single space, browser-like.
    const auto emit_text = [&](const std::string& raw) {
        std::string t;
        t.reserve(raw.size());
        bool ws_pending = false, any = false;
        for (char c : raw) {
            if (is_space(c)) {
                ws_pending = true;
            } else {
                if (ws_pending && any) t += ' ';
                t += c;
                ws_pending = false;
                any = true;
            }
        }
        if (t.empty()) return;
        if (ws_pending && any) t += ' ';  // keep one trailing space of the run
        runs.push_back(StyledText{t, font, size, weight, italic, strike, underline,
                                  color, bg, false, "", ""});
        text_pushed = true;
    };

    // Helper: emit a hard line break inside the current block (one P block with
    // multiple lines — supported by the draw layer because single_paragraph_mode=false).
    const auto newline_run = [&]() -> StyledText {
        return StyledText{"\n", base_font, base_size_pt, 400, false, false, false,
                          base_color, Color{}, false, "", ""};
    };
    // Helper: list marker for the active ul/ol wrapper.
    const auto list_marker = [&]() -> StyledText {
        std::string m = (wrapper_name == "ol") ? (std::to_string(ol_count++) + ". ") : "\xE2\x80\xA2 ";
        return StyledText{m, base_font, base_size_pt, 400, false, false, false,
                          base_color, Color{}, false, "", ""};
    };

    const auto pop_scope = [&]() {
        if (stack.empty()) return;
        const Scope sc = stack.back();
        stack.pop_back();
        font = sc.font;
        size = sc.size;
        weight = sc.weight;
        italic = sc.italic;
        strike = sc.strike;
        underline = sc.underline;
        color = sc.color;
        bg = sc.bg;
    };

    std::size_t pos = 0;
    const std::size_t n = s.size();
    while (pos < n) {
        const std::size_t lt = s.find('<', pos);
        if (lt == std::string::npos) {
            emit_text(s.substr(pos));
            break;
        }
        if (lt > pos) emit_text(s.substr(pos, lt - pos));

        if (s.compare(lt, 4, "<!--") == 0) {  // comment
            const std::size_t end = s.find("-->", lt + 4);
            if (end == std::string::npos) break;
            pos = end + 3;
            continue;
        }
        if (lt + 1 < n && (s[lt + 1] == '!' || s[lt + 1] == '?')) {
            const std::size_t end = s.find('>', lt + 2);
            if (end == std::string::npos) {
                emit_text("<");
                pos = lt + 1;
                continue;
            }
            pos = end + 1;
            continue;
        }

        const auto tag = parse_html_tag(s.substr(lt));
        if (!tag) {
            emit_text("<");
            pos = lt + 1;
            continue;
        }
        const bool first_tag = !tag_parsed;
        const bool block = is_block_level_tag(tag->name);
        const bool in_list_wrapper = wrapper_open && (wrapper_name == "ul" || wrapper_name == "ol");

        if (block) {
            // Inside an open <ul>/<ol>: handle <li> open/close.
            if (in_list_wrapper && tag->name == "li") {
                if (tag->closing) {
                    if (in_li) { pop_scope(); in_li = false; }
                } else {
                    if (!first_li) runs.push_back(newline_run());
                    first_li = false;
                    runs.push_back(list_marker());
                    Scope sc{"li", font, size, weight, italic, strike, underline, color, bg};
                    stack.push_back(sc);
                    in_li = true;
                }
                tag_parsed = true;
                pos = lt + tag->consumed;
                continue;
            }
            // Closing the list wrapper (</ul> or </ol>) — handle before nested check.
            if (in_list_wrapper && tag->closing && (tag->name == "ul" || tag->name == "ol")) {
                if (!wrapper_open || tag->name != wrapper_name) return false;
                if (in_li) { pop_scope(); in_li = false; }
                pop_scope();
                wrapper_open = false;
                in_list = false;
                wrapper_name.clear();
                ol_count = 1;
                tag_parsed = true;
                pos = lt + tag->consumed;
                continue;
            }
            // Nested <ul>/<ol> inside a list wrapper (opening only): unsupported.
            if (in_list_wrapper && !tag->closing && (tag->name == "ul" || tag->name == "ol")) return false;

            if (tag->closing) {
                if (!wrapper_open || tag->name != wrapper_name) return false;
                if (in_list_wrapper && in_li) { pop_scope(); in_li = false; }
                pop_scope();
                wrapper_open = false;
                in_list = false;
                wrapper_name.clear();
                ol_count = 1;
                first_li = true;
            } else if (tag->self_closing || wrapper_open || !first_tag || text_pushed) {
                return false;
            } else {
                if (tag->name == "ul" || tag->name == "ol") {
                    in_list = true;
                    ol_count = 1;
                    first_li = true;
                }
                Scope sc{tag->name, font, size, weight, italic, strike, underline,
                         color, bg};
                stack.push_back(sc);
                apply(style_from_tag(tag->name, tag->style, style));
                wrapper_open = true;
                wrapper_name = tag->name;
            }
            tag_parsed = true;
            pos = lt + tag->consumed;
            continue;
        }

        if (tag->closing) {
            if (!stack.empty() && stack.back().name == tag->name) pop_scope();
        } else if (tag->self_closing || tag->name == "br") {
            if (tag->name == "br") runs.push_back(newline_run());
        } else {
            Scope sc{tag->name, font, size, weight, italic, strike, underline,
                     color, bg};
            stack.push_back(sc);
            apply(style_from_tag(tag->name, tag->style, style));
        }
        tag_parsed = true;
        pos = lt + tag->consumed;
    }

    if (runs.empty()) return false;
    out.insert(out.end(), runs.begin(), runs.end());
    return true;
}

} // namespace remin::markdown