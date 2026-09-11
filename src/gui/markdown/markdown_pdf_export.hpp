#pragma once

#include "gui/markdown/markdown_ast.hpp"
#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace remin::markdown {

// Header/footer line options. Every field may contain the tokens below, which
// are replaced per page:
//   {page} {pages} {date} {time} {title} {author} {filename}
struct PdfPageMeta {
    std::string title;
    std::string author;

    std::string header_left;
    std::string header_center;
    std::string header_right = "{page}";

    std::string footer_left = "{title}";
    std::string footer_center;
    std::string footer_right = "{page} / {pages}";
};

// One PDF page: contiguous half-open block range [first, last].
struct PdfPageSlice {
    std::size_t first = 0;
    std::size_t last = 0;        // included index
    double start_y = 0.0;
};

// Paginate laid-out blocks into page slices that each fit `page_height_pt`.
// Block::Kind::PageBreak acts as a hard page boundary (skipped from slices, so
// no empty pages and the preview marker is never drawn in the PDF). Exposed for
// unit tests; export_pdf() uses it internally.
[[nodiscard]] std::vector<PdfPageSlice> paginate_blocks(
    const std::vector<Block>& blocks, double page_height_pt);

// Export a Markdown document to PDF using the same shared layout/draw pipeline
// as the preview. Images are resolved through `resolve_image`. Returns false
// if the target path cannot be written.
[[nodiscard]] bool export_pdf(const MarkdownAst& ast,
                              const StyleSheet& style,
                              const ImageResolver& resolve_image,
                              const std::filesystem::path& out_path,
                              const PdfPageMeta& meta);

} // namespace remin::markdown