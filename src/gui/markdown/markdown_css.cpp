#include "gui/markdown/markdown_css.hpp"

#include <fstream>
#include <sstream>

namespace remin::markdown {

namespace {

// Minimal but valid fallback stylesheet. The real theme lives in
// resources/styles/markdown-preview.css; this only guards against a broken
// resource layout at runtime.
const char* kFallbackCss = R"(body {
  font-family: -apple-system, "Segoe UI", system-ui, sans-serif;
  line-height: 1.6;
  color: #1f2328;
  max-width: 42rem;
  margin: 0 auto;
  padding: 1.5rem 2rem;
}
h1, h2, h3, h4, h5, h6 { line-height: 1.2; margin: 1.4em 0 0.5em; }
code { background: #f0f1f3; border-radius: 4px; padding: 0.15em 0.35em; }
pre { background: #f6f8fa; border: 1px solid #e0e3e7; border-radius: 6px; padding: 0.75em 1em; overflow: auto; }
pre code { background: none; padding: 0; }
blockquote { border-left: 4px solid #d0d7de; margin: 0; padding-left: 1em; color: #59636e; }
table { border-collapse: collapse; width: 100%; }
th, td { border: 1px solid #d0d7de; padding: 0.35em 0.6em; text-align: left; }
th { background: #f6f8fa; }
> body img { max-width: 100%; }
)";

} // namespace

std::string builtin_preview_css() {
    const std::string path =
        std::string(REMIN_RESOURCE_DIR) + "/styles/markdown-preview.css";
    std::ifstream in(path);
    if (!in) return kFallbackCss;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string css = ss.str();
    if (css.empty()) return kFallbackCss;
    return css;
}

std::string read_user_css(const std::string& path) {
    if (path.empty()) return {};
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string build_html_document(const std::string& body, const std::string& css,
                                const std::string& title) {
    std::string html = "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    if (!title.empty())
        html += "<title>" + title + "</title>\n";
    else
        html += "<title>Preview</title>\n";
    html += "<style>\n" + css + "\n</style>\n</head>\n<body>\n<main>\n";
    html += body;
    html += "</main>\n</body>\n</html>\n";
    return html;
}

} // namespace remin::markdown