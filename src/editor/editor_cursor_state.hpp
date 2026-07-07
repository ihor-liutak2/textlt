#pragma once

#include <cstddef>

namespace textlt {

struct Selection {
    bool active = false;
    size_t anchor_x = 0;
    size_t anchor_y = 0;
};

struct EditorCursorState {
    size_t cursor_row = 0;
    size_t cursor_col = 0;
    Selection selection;
};

} // namespace textlt
