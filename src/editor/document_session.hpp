#pragma once

#include <cstddef>

namespace textlt {

struct Selection {
    bool active = false;
    size_t anchor_x = 0;
    size_t anchor_y = 0;
};

struct DocumentSession {
    size_t cursor_row = 0;
    size_t cursor_col = 0;
    Selection selection;
    size_t scroll_x = 0;
    size_t scroll_y = 0;
};

using EditorSession = DocumentSession;

} // namespace textlt
