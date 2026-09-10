#pragma once

#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <pangomm.h>
#include <pangomm/cairofontmap.h>

namespace remin::markdown {

// Shared Pango glue between the layout engine (measure) and the drawing layer
// (render). Both build the layout through the exact same code path so wrapped
// line breaks and metrics are identical in the preview and the PDF.

// A Pango Context at 72 dpi (1 Pango device unit == 1 pt == 1 Cairo pt).
Glib::RefPtr<Pango::Context> measure_context();

// Apply styled runs to `layout`: builds text + full attribute list, sets wrap
// width and word-char wrapping. Call on the resolution-set context before
// measuring or drawing.
void apply_styled_runs(Pango::Layout& layout, const std::vector<StyledText>& runs,
                       double width_pt, const StyleSheet& style);

struct PangoMetrics {
    double width = 0.0;       // widest line (pt)
    double height = 0.0;      // total layout height (pt)
    double baseline_pt = 0.0; // first-line baseline offset
    double line_pt = 0.0;     // average line height (height / line count)
    int line_count = 1;
};

// Measure a set of styled runs wrapped at width_pt (<=0 => no wrap).
[[nodiscard]] PangoMetrics measure_runs(const std::vector<StyledText>& runs,
                                        double width_pt, const StyleSheet& style);

} // namespace remin::markdown