// Unit tests for the inline HTML/CSS subset used by the native preview + PDF
// renderers (NOT the HTML export, which stays a verbatim pass-through).
//
// Pure subject: CSS declaration parsing, tag semantic merging, HTML tag
// tokenizing, whole-chunk rendering, and the Case-A layout wrapper context.
// No Gtk display is required (headless pangomm), mirroring markdown_layout_test.
#include "gui/markdown/markdown_inline_html.hpp"
#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <pangomm/init.h>

using namespace remin::markdown;

namespace {

int fails = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        ++fails;
        std::cerr << "FAIL: " << msg << "\n";
    }
}

bool is_red(const Color& c) {
    return c.valid && std::fabs(c.r - 1.0f) < 1e-4f &&
           std::fabs(c.g) < 1e-4f && std::fabs(c.b) < 1e-4f;
}

bool is_blue(const Color& c) {
    return c.valid && std::fabs(c.b - 1.0f) < 1e-4f &&
           std::fabs(c.r) < 1e-4f && std::fabs(c.g) < 1e-4f;
}

} // namespace

int main() {
    Pango::init();
    const StyleSheet style = default_style(light_palette());

    // --- parse_css_declaration: valid multi-declaration string ---
    {
        const auto s = parse_css_declaration("color:red; background-color:#fee; "
                                             "font-weight:bold; font-style:italic; "
                                             "text-decoration:underline; font-size:12pt");
        check(s.has_color && is_red(s.color), "css color red");
        check(s.has_bg && s.background.valid, "css background #fee valid");
        check(s.weight == 700, "css font-weight bold -> 700");
        check(s.italic_set && s.italic, "css font-style italic");
        check(s.underline_set && s.underline, "css text-decoration underline");
        check(!s.strike_set, "css text-decoration no strike");
        check(s.size_pt == 12.0, "css font-size 12pt");
    }
    {
        const auto s = parse_css_declaration("font-size:150%");
        check(s.size_pt == 0.0 && std::fabs(s.size_pct - 1.5) < 1e-4,
              "css font-size 150% -> pct ~1.5");
        const auto p = parse_css_declaration("font-size:18px");
        check(std::fabs(p.size_pt - 13.5) < 1e-6, "css font-size 18px -> 13.5pt");
    }

    // --- parse_css_declaration: malformed, never crash, valid parts win ---
    {
        const auto a = parse_css_declaration("color:");
        check(!a.has_color, "empty color value ignored");
        const auto b = parse_css_declaration(";;;");
        check(!b.has_color && b.weight == 0, "empty declarations ignored");
        const auto c = parse_css_declaration("bogus:x;;color:red");
        check(c.has_color && is_red(c.color), "unknown prop ignored, color kept");
        const auto d = parse_css_declaration("color: !!!");
        check(!d.has_color, "garbage color ignored");
        const auto e = parse_css_declaration("font-size:abc");
        check(e.size_pt == 0.0 && e.size_pct == 0.0, "bad font-size ignored");
    }

    // --- parse_css_declaration color forms ---
    {
        check(parse_css_declaration("color:red").color.valid, "named color valid");
        {
            const auto s = parse_css_declaration("color:#f00");
            check(s.has_color, "#rgb valid");
            check(is_red(s.color), "#rgb expands to red");
        }
        check(parse_css_declaration("color:#ff0000").color.valid, "#rrggbb valid");
        {
            const auto s = parse_css_declaration("color:#ff000080");
            check(s.has_color && std::fabs(s.color.a - 128.0f / 255.0f) < 1e-4f,
                  "#rrggbbaa alpha captured");
        }
        {
            const auto s = parse_css_declaration("color:rgb(10, 20, 30)");
            check(s.has_color && std::fabs(s.color.r - 10.0f / 255.0f) < 1e-4f,
                  "rgb(10,20,30) red channel");
        }
        {
            const auto s = parse_css_declaration("color:rgba(255, 0, 0, 0.5)");
            check(s.has_color && std::fabs(s.color.a - 0.5f) < 1e-4f,
                  "rgba alpha ~0.5");
        }
        check(parse_css_declaration("color:rgb(50%, 0%, 0%)").color.valid,
              "rgb percentages valid");
        check(!parse_css_declaration("color:notacolor").has_color, "unknown keyword invalid");
        check(!parse_css_declaration("color:#zzz").has_color, "bad hex invalid");
        check(!parse_css_declaration("color:rgb(1,2)").has_color, "short rgb invalid");
        check(!parse_css_declaration("color:rgb(1,2,3,4)").has_color, "long rgb invalid");
    }

    // --- style_from_tag semantic defaults + CSS override wins ---
    {
        check(style_from_tag("b", "", style).weight == 700, "b -> weight 700");
        check(style_from_tag("strong", "", style).weight == 700, "strong -> weight 700");
        {
            const auto s = style_from_tag("i", "", style);
            check(s.italic_set && s.italic, "i -> italic");
        }
        {
            const auto s = style_from_tag("u", "", style);
            check(s.underline_set && s.underline, "u -> underline");
        }
        {
            const auto s = style_from_tag("s", "", style);
            check(s.strike_set && s.strike, "s -> strike");
        }
        {
            const auto s = style_from_tag("code", "", style);
            check(s.font_family == "monospace" && s.has_bg, "code -> mono + bg");
        }
        {
            const auto s = style_from_tag("mark", "", style);
            check(s.has_bg && s.background.valid, "mark -> background");
        }
        {
            const auto s = style_from_tag("span", "", style);
            check(s.weight == 0 && !s.has_color && !s.italic_set && !s.has_bg,
                  "span -> no defaults");
        }
        {
            const auto s = style_from_tag("b", "font-weight:normal", style);
            check(s.weight == 400, "css font-weight overrides b -> 400");
        }
        {
            const auto s = style_from_tag("span", "color:blue", style);
            check(s.has_color && is_blue(s.color), "span style color blue");
        }
    }

    // --- parse_html_tag ---
    {
        const std::string raw = "<span style=\"color:red\">";
        const auto tag = parse_html_tag(raw);
        check(tag.has_value(), "span tag parsed");
        if (tag) {
            check(tag->name == "span", "span name");
            check(tag->style == "color:red", "span style attr");
            check(!tag->closing && !tag->self_closing, "span open not closing");
            check(tag->consumed == raw.size(), "span consumes whole input");
        }
        if (const auto b = parse_html_tag("<b>")) {
            check(b->name == "b" && !b->closing && !b->self_closing &&
                      b->consumed == 3,
                  "b open tag");
        }
        if (const auto c = parse_html_tag("</b>")) {
            check(c->name == "b" && c->closing, "b close tag");
        }
        if (const auto br = parse_html_tag("<br/>")) {
            check(br->name == "br" && br->self_closing && !br->closing,
                  "br self-closing");
        }
        check(!parse_html_tag("<!-- x -->"), "comment rejected");
        check(!parse_html_tag("<!DOCTYPE html>"), "doctype rejected");
        check(!parse_html_tag("<a"), "unterminated tag rejected");
        if (const auto div = parse_html_tag("<div class=\"x\" style='y' >")) {
            check(div->name == "div" && div->style == "y",
                  "style value quotes stripped");
        }
    }

    // --- is_block_level_tag ---
    {
        check(is_block_level_tag("div"), "div block");
        check(is_block_level_tag("section"), "section block");
        check(is_block_level_tag("p"), "p block");
        check(is_block_level_tag("h1"), "h1 block");
        check(is_block_level_tag("table"), "table block");
        check(!is_block_level_tag("span"), "span inline");
        check(!is_block_level_tag("b"), "b inline");
        check(!is_block_level_tag("code"), "code inline");
        check(!is_block_level_tag("em"), "em inline");
        check(!is_block_level_tag("mark"), "mark inline");
    }

    // --- render_html_block_runs Case B: whole chunk with wrapper ---
    {
        std::vector<StyledText> runs;
        const bool ok = render_html_block_runs(
            "<div style=\"color:blue\">Hello <b>world</b></div>\n", style,
            style.base_font, style.base_font_pt, style.text_color, runs);
        check(ok, "chunk rendered");
        bool hello_ok = false, world_ok = false;
        for (const auto& r : runs) {
            if (r.text == "Hello ") {
                hello_ok = is_blue(r.color) && r.weight == 400;
            } else if (r.text == "world") {
                world_ok = is_blue(r.color) && r.weight == 700;
            }
        }
        check(hello_ok, "Hello run blue + weight 400");
        check(world_ok, "world run blue + weight 700");
    }

    // --- render_html_block_runs: nested block-level is dropped ---
    {
        std::vector<StyledText> runs;
        const bool ok = render_html_block_runs(
            "<div style=\"color:blue\">Hello <table><tr><td>x</td></tr></table></div>",
            style, style.base_font, style.base_font_pt, style.text_color, runs);
        check(!ok, "nested block-level chunk rejected");
    }

    // --- render_html_block_runs: comment + declaration skipped safely ---
    {
        std::vector<StyledText> runs;
        const bool ok = render_html_block_runs(
            "<div><!-- c --><b>A</b> <i>B</i></div>", style,
            style.base_font, style.base_font_pt, style.text_color, runs);
        check(ok, "comment chunk rendered");
        bool a_ok = false, b_ok = false;
        for (const auto& r : runs) {
            if (r.text == "A") a_ok = r.weight == 700;
            if (r.text == "B") b_ok = r.italic;
        }
        check(a_ok && b_ok, "inline tags inside wrapper applied");
    }

    // --- Layout round-trip Case A: wrapper style reaches the paragraph ---
    {
        const auto ast = MarkdownAst::parse(
            "<div style=\"color:red\">\n\n**Bold** text\n\n</div>");
        const auto result = layout_document(ast, style, style.content_width_pt(), {});
        bool bold_red = false;
        bool text_red = false;
        for (const auto& b : result.blocks) {
            for (const auto& run : b.run) {
                if (run.text == "Bold" && is_red(run.color) && run.weight == 700)
                    bold_red = true;
                if (run.text == " text" && is_red(run.color) && run.weight == 400)
                    text_red = true;
            }
        }
        check(bold_red, "case A: bold run red + weight 700");
        check(text_red, "case A: plain run red + weight 400");
    }

    // --- Malformed inputs never crash; layout still yields blocks ---
    {
        const auto ast = MarkdownAst::parse(
            "Before\n\n<span style=\"color:red\"\n\nAfter\n\n"
            "<div>\n\nAgain\n\n<a href=\"x\">y</a>\n\nEnd");
        const auto result = layout_document(ast, style, style.content_width_pt(), {});
        check(!result.blocks.empty(), "malformed html still lays out");
        bool saw_after = false;
        for (const auto& b : result.blocks)
            for (const auto& run : b.run)
                if (run.text.find("After") != std::string::npos) saw_after = true;
        check(saw_after, "plain text around malformed html kept");
    }

    // --- HTML <ul>/<ol> + <br> rendering (case B whole-chunk) ---
    {
        std::vector<StyledText> runs;
        const auto s = style;
        // ul
        bool ok = render_html_block_runs(
            "<ul><li>first</li><li>second</li></ul>", s, "sans", 11.0, style.text_color, runs);
        check(ok, "ul renders true");
        check(!runs.empty(), "ul produces runs");
        // concatenate text
        std::string all;
        for (const auto& r : runs) all += r.text;
        check(all.find("first") != std::string::npos, "ul item 1 present");
        check(all.find("second") != std::string::npos, "ul item 2 present");
        check(all.find("\n") != std::string::npos, "ul items separated by newline");
        check(all.find("\xE2\x80\xA2") != std::string::npos, "ul bullet marker present");
    }
    {
        std::vector<StyledText> runs;
        const auto s = style;
        // ol
        bool ok = render_html_block_runs(
            "<ol><li>one</li><li>two</li></ol>", s, "sans", 11.0, style.text_color, runs);
        check(ok, "ol renders true");
        check(!runs.empty(), "ol produces runs");
        std::string all;
        for (const auto& r : runs) all += r.text;
        check(all.find("1. one") != std::string::npos, "ol counter 1");
        check(all.find("2. two") != std::string::npos, "ol counter 2");
    }
    {
        std::vector<StyledText> runs;
        const auto s = style;
        // nested list -> fail (return false)
        bool ok = render_html_block_runs(
            "<ul><li>a<ul><li>b</li></ul></li></ul>", s, "sans", 11.0, style.text_color, runs);
        check(!ok, "nested ul returns false");
    }
    {
        std::vector<StyledText> runs;
        const auto s = style;
        // <br/> self-closing produces newline
        bool ok = render_html_block_runs(
            "hello<br/>world", s, "sans", 11.0, style.text_color, runs);
        check(ok, "br self-closing renders true");
        std::string all;
        for (const auto& r : runs) all += r.text;
        check(all.find("\n") != std::string::npos, "br produces newline");
    }
    {
        std::vector<StyledText> runs;
        const auto s = style;
        // plain <br> (no slash) also produces newline
        bool ok = render_html_block_runs(
            "line1<br>line2", s, "sans", 11.0, style.text_color, runs);
        check(ok, "plain br renders true");
        std::string all;
        for (const auto& r : runs) all += r.text;
        check(all.find("\n") != std::string::npos, "plain br produces newline");
    }

    if (fails == 0) {
        std::cout << "markdown_inline_html_test: ALL PASS\n";
        return 0;
    }
    std::cerr << "markdown_inline_html_test: " << fails << " failure(s)\n";
    return 1;
}