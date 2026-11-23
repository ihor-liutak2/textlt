#pragma once

#include <algorithm>
#include <string>

#include "ftxui/screen/box.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt {

inline std::string VisibleLabel(const std::string& label, int offset, int width) {
    if (offset == 0 && ftxui::string_width(label) <= width) {
        return label;
    }
    const auto glyphs = ftxui::Utf8ToGlyphs(label);
    const int end = std::min(offset + width, static_cast<int>(glyphs.size()));
    std::string visible;
    for (int column = offset; column < end; ++column) {
        const std::string& glyph = glyphs[column];
        const int glyph_width = ftxui::string_width(glyph);
        // A full-width glyph must fit on both sides of the viewport.
        if (column == offset && glyph_width == 0) {
            visible += ' ';
        } else if (column != end - 1 || glyph_width != 2) {
            visible += glyph;
        }
    }
    return visible;
}

inline int BoxWidth(const ftxui::Box& box) {
    return box.IsEmpty() ? 0 : box.x_max - box.x_min + 1;
}

} // namespace textlt
