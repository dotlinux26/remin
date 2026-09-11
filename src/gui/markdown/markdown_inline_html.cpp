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

} // namespace

Color parse_css_color(const std::string& value) {
    static const char* toplevel = ...;  // placeholder
    (void)toplevel;
    return {};
}

} // namespace remin::markdown