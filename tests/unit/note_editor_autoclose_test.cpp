// Unit test for the VS Code-style HTML tag auto-close helper.
// Pure function, no GTK dependency.
#include "gui/note/note_editor.hpp"

#include <cstdio>
#include <iostream>
#include <string>

using namespace remin::gui;

namespace {

int fails = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        ++fails;
        std::cerr << "FAIL: " << msg << "\n";
    }
}

} // namespace

int main() {
    // Basic opening tag
    check(suggest_closing_html_tag("<h1", 3) == "</h1>", "basic <h1");
    check(suggest_closing_html_tag("<div", 4) == "</div>", "basic <div");
    check(suggest_closing_html_tag("<span", 5) == "</span>", "basic <span");

    // With attributes
    check(suggest_closing_html_tag("<h1 class=\"title\"", 17) == "</h1>", "with attributes");
    check(suggest_closing_html_tag("<div id=main", 12) == "</div>", "with unquoted attr");
    check(suggest_closing_html_tag("<a href=\"#x\"", 12) == "</a>", "link attr");

    // Already closed (should not trigger)
    check(suggest_closing_html_tag("<h1>", 4).empty(), "already closed <h1>");
    check(suggest_closing_html_tag("<div>text", 8).empty(), "already closed <div>text");

    // Closing tag typed (</h1>) — don't double-close
    check(suggest_closing_html_tag("</h1", 4).empty(), "closing tag </h1");
    check(suggest_closing_html_tag("</div", 5).empty(), "closing tag </div");

    // Comment or declaration
    check(suggest_closing_html_tag("<!-- comment", 12).empty(), "comment");
    check(suggest_closing_html_tag("<!DOCTYPE html", 14).empty(), "doctype");

    // Comparison operator "a > b" — no '<' before cursor
    check(suggest_closing_html_tag("a > b", 5).empty(), "comparison a > b");
    // But "a < b >" — still no valid tag name
    check(suggest_closing_html_tag("a < b >", 7).empty(), "a < b >");

    // Void elements — never auto-close
    check(suggest_closing_html_tag("<br", 3).empty(), "void <br");
    check(suggest_closing_html_tag("<img", 4).empty(), "void <img");
    check(suggest_closing_html_tag("<hr", 3).empty(), "void <hr");
    check(suggest_closing_html_tag("<input", 6).empty(), "void <input");
    check(suggest_closing_html_tag("<meta", 5).empty(), "void <meta");
    check(suggest_closing_html_tag("<link", 5).empty(), "void <link");
    check(suggest_closing_html_tag("<area", 5).empty(), "void <area");
    check(suggest_closing_html_tag("<base", 5).empty(), "void <base");
    check(suggest_closing_html_tag("<col", 4).empty(), "void <col");
    check(suggest_closing_html_tag("<embed", 6).empty(), "void <embed");
    check(suggest_closing_html_tag("<source", 7).empty(), "void <source");
    check(suggest_closing_html_tag("<track", 6).empty(), "void <track");
    check(suggest_closing_html_tag("<wbr", 4).empty(), "void <wbr");

    // Invalid tag name (starts with digit, empty, etc.)
    check(suggest_closing_html_tag("<123", 4).empty(), "starts with digit");
    check(suggest_closing_html_tag("<", 1).empty(), "just <");
    check(suggest_closing_html_tag("< >", 3).empty(), "< >");

    // Multiple '<' on line — pick the nearest (VS Code behavior)
    check(suggest_closing_html_tag("<div><span", 10) == "</span>", "nested picks inner");

    // Case-insensitive tag name, output lowercase
    check(suggest_closing_html_tag("<H1", 3) == "</h1>", "uppercase -> lowercase");
    check(suggest_closing_html_tag("<DiV", 4) == "</div>", "mixed case -> lowercase");

    if (fails == 0) {
        std::cout << "note_editor_autoclose_test: ALL PASS\n";
        return 0;
    }
    std::cerr << "note_editor_autoclose_test: " << fails << " failure(s)\n";
    return 1;
}