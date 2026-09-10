#pragma once

#include "gui/markdown/markdown_ast.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace remin::markdown {

// Print/export settings for the note's PDF output (persisted per note).
struct PrintConfig {
    std::string header;               // literal header text (top of each page)
    std::string footer;               // literal footer text (bottom of each page)
    bool show_page_numbers = true;    // "Page N of M" appended to the footer
    bool show_toc = true;             // render [[TOC]] as a front TOC page
    int max_toc_depth = 6;
    std::string font_family;          // empty = Pango default
    double base_font_pt = 11.0;

    [[nodiscard]] std::string to_json() const;
    static PrintConfig from_json(const std::string& json);
};

// The per-note Markdown document model: source, parsed AST, asset/css
// locations and print settings. The preview, HTML export and PDF export layers
// all read from here; pasted images are saved through this class so the
// reference written into the source always matches the physical file.
class MarkdownDocument {
public:
    // Re-parse `source` (idempotent, cheap relative to a keystroke).
    void set_source(const std::string& source) {
        source_ = source;
        ast_ = MarkdownAst::parse(source);
    }
    [[nodiscard]] const std::string& source() const { return source_; }
    [[nodiscard]] const MarkdownAst& ast() const { return ast_; }

    // Directory that holds the note file (parent of the note). Relative asset
    // refs resolve against it; the assets/ subfolder lives below it.
    void set_note_dir(std::filesystem::path dir) { note_dir_ = std::move(dir); }
    [[nodiscard]] const std::filesystem::path& note_dir() const { return note_dir_; }

    // Directory where pasted images are stored (normally note_dir_/assets).
    void set_asset_dir(std::filesystem::path dir) { asset_dir_ = std::move(dir); }
    [[nodiscard]] const std::filesystem::path& asset_dir() const { return asset_dir_; }

    // User-selected preview/export CSS path (empty = builtin stylesheet).
    void set_css_path(std::string path) { css_path_ = std::move(path); }
    [[nodiscard]] const std::string& css_path() const { return css_path_; }

    // Save PNG bytes as the next free asset-NNN.png under asset_dir. Returns
    // the markdown reference to paste into the source ("assets/asset-003.png")
    // or nullopt when the directory is unavailable or the write failed.
    [[nodiscard]] std::optional<std::string> save_pasted_image(
        const std::string& png_bytes) const;

    // Resolve a markdown image reference to an absolute filesystem path when
    // it points at a local asset ("assets/x.png", absolute path, or a path
    // relative to note_dir_). Returns nullopt for remote refs (http(s), data:).
    [[nodiscard]] std::optional<std::filesystem::path> resolve_asset(
        const std::string& ref) const;

    PrintConfig& print_config() { return print_config_; }
    [[nodiscard]] const PrintConfig& print_config() const { return print_config_; }

private:
    std::string source_;
    MarkdownAst ast_;
    std::filesystem::path note_dir_;
    std::filesystem::path asset_dir_;
    std::string css_path_;
    PrintConfig print_config_;
};

} // namespace remin::markdown