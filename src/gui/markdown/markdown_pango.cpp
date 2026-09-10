#include "gui/markdown/markdown_pango.hpp"

#include <algorithm>

namespace remin::markdown {

namespace {
constexpr int kScale = PANGO_SCALE;
}

Glib::RefPtr<Pango::Context> measure_context() {
    static const Glib::RefPtr<Pango::Context> ctx =
        [] {
            auto fm = Pango::CairoFontMap::get_default();
            auto c = fm->create_context();
            c->set_resolution(72.0);  // 1 Pango device unit == 1 pt
            return c;
        }();
    return ctx;
}

void apply_styled_runs(Pango::Layout& layout, const std::vector<StyledText>& runs,
                       double width_pt, const StyleSheet& style) {
    // Base font fallback so plain text never renders in a default serif.
    Pango::FontDescription fd(style.base_font);
    fd.set_absolute_size(style.base_font_pt * kScale);
    layout.set_font_description(fd);

    std::string text;
    text.reserve(4096);
    Pango::AttrList attrs;

    for (const StyledText& run : runs) {
        if (run.text.empty()) continue;
        const guint start = static_cast<guint>(text.size());
        text += run.text;
        const guint end = static_cast<guint>(text.size());

        Pango::Attribute size = Pango::Attribute::create_attr_size_absolute(
            static_cast<int>(run.size_pt * kScale));
        size.set_start_index(start);
        size.set_end_index(end);
        attrs.insert(size);

        Pango::Attribute fam = Pango::Attribute::create_attr_family(
            run.font_family.empty() ? style.base_font : run.font_family);
        fam.set_start_index(start);
        fam.set_end_index(end);
        attrs.insert(fam);

        Pango::Attribute wt = Pango::Attribute::create_attr_weight(
            static_cast<Pango::Weight>(run.weight));
        wt.set_start_index(start);
        wt.set_end_index(end);
        attrs.insert(wt);

        Pango::Attribute st = Pango::Attribute::create_attr_style(
            run.italic ? Pango::Style::ITALIC : Pango::Style::NORMAL);
        st.set_start_index(start);
        st.set_end_index(end);
        attrs.insert(st);

        Pango::Attribute ul = Pango::Attribute::create_attr_underline(
            run.underline ? Pango::Underline::SINGLE : Pango::Underline::NONE);
        ul.set_start_index(start);
        ul.set_end_index(end);
        attrs.insert(ul);

        Pango::Attribute sk = Pango::Attribute::create_attr_strikethrough(run.strike);
        sk.set_start_index(start);
        sk.set_end_index(end);
        attrs.insert(sk);

        if (run.color.valid) {
            auto norm = [](float c) -> guint16 {
                return static_cast<guint16>(std::clamp(c, 0.0f, 1.0f) * 65535.0f);
            };
            Pango::Attribute fg = Pango::Attribute::create_attr_foreground(
                norm(run.color.r), norm(run.color.g), norm(run.color.b));
            fg.set_start_index(start);
            fg.set_end_index(end);
            attrs.insert(fg);
        }
        if (run.background.valid) {
            auto norm = [](float c) -> guint16 {
                return static_cast<guint16>(std::clamp(c, 0.0f, 1.0f) * 65535.0f);
            };
            Pango::Attribute bg = Pango::Attribute::create_attr_background(
                norm(run.background.r), norm(run.background.g), norm(run.background.b));
            bg.set_start_index(start);
            bg.set_end_index(end);
            attrs.insert(bg);
        }
    }

    layout.set_text(text);
    layout.set_attributes(attrs);
    if (width_pt > 0.0) layout.set_width(static_cast<int>(width_pt * kScale));
    else layout.set_width(-1);  // unconstrained single wide line
    layout.set_wrap(Pango::WrapMode::WORD_CHAR);
    layout.set_single_paragraph_mode(false);
}

PangoMetrics measure_runs(const std::vector<StyledText>& runs, double width_pt,
                          const StyleSheet& style) {
    auto layout = Pango::Layout::create(measure_context());
    apply_styled_runs(*layout, runs, width_pt, style);
    int w = 0, h = 0;
    layout->get_size(w, h);
    const int lines = std::max(layout->get_line_count(), 1);
    const double width = static_cast<double>(w) / PANGO_SCALE;
    const double height = static_cast<double>(h) / PANGO_SCALE;
    PangoMetrics m;
    m.width = width;
    m.height = height;
    m.baseline_pt = static_cast<double>(layout->get_baseline()) / PANGO_SCALE;
    m.line_pt = height / static_cast<double>(lines);
    m.line_count = lines;
    return m;
}

} // namespace remin::markdown