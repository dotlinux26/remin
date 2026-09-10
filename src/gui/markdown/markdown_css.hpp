#pragma once

#include <string>

namespace remin::markdown {

// Built-in preview stylesheet (resources/styles/markdown-preview.css when the
// resource dir is available at runtime; otherwise an embedded minimal fallback
// so preview never breaks on mis-packaged installs).
[[nodiscard]] std::string builtin_preview_css();

// Read a user-selected CSS file (Settings > Markdown CSS). Returns empty when
// the path is missing/unreadable so callers can fall back to the builtin.
[[nodiscard]] std::string read_user_css(const std::string& path);

// Wrap a rendered HTML body (from markdown_html) plus a stylesheet into a
// complete, self-contained document for the preview pane.
[[nodiscard]] std::string build_html_document(const std::string& body,
                                              const std::string& css,
                                              const std::string& title);

} // namespace remin::markdown