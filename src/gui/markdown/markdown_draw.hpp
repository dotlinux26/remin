#pragma once

#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <cairomm/cairomm.h>
#include <string>
#include <vector>

namespace remin::markdown {

// Draws a laid-out document flow (`LayoutResult::blocks`) into a Cairo
// context. The caller translates the context to the flow origin; blocks carry
// absolute y coordinates. This same function renders the GTK preview (screen
// surface) and the PDF export (PDF surface) so both share one pipeline.
void draw_flow(const Cairo::RefPtr<Cairo::Context>& cr,
               const std::vector<Block>& blocks,
               const StyleSheet& style);

// Draw only blocks in the half-open index range [begin, end). Used by the PDF
// exporter to render one page of the flow (origin already translated).
// If `justify` is true, multi-line text is justified (PDF printing).
// If `emit_links` is true (PDF export) internal links and destinations are
// emitted as PDF tags: every heading is a named dest and every block with an
// internal "#anchor" (TOC rows, link paragraphs) becomes a clickable Link.
void draw_blocks_range(const Cairo::RefPtr<Cairo::Context>& cr,
                       const std::vector<Block>& blocks,
                       std::size_t begin, std::size_t end,
                       const StyleSheet& style, bool justify = false,
                       bool emit_links = false);

// Clickable regions for the preview (links inside blocks).
struct HitRegion {
    double x = 0, y = 0, w = 0, h = 0;
    std::string href;  // "#anchor" or full URL
};
[[nodiscard]] std::vector<HitRegion>
collect_hit_regions(const std::vector<Block>& blocks);

} // namespace remin::markdown