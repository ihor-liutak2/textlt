#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace textlt::transform {

struct CursorState {
    size_t x = 0;
    size_t y = 0;
};

struct SelectionState {
    bool active = false;
    size_t anchor_x = 0;
    size_t anchor_y = 0;
};

struct TransformResult {
    bool changed = false;
    CursorState cursor;
    SelectionState selection;
};

TransformResult Convert4To2Spaces(std::vector<std::string>& lines,
                                  CursorState cursor,
                                  SelectionState selection);
TransformResult Convert2To4Spaces(std::vector<std::string>& lines,
                                  CursorState cursor,
                                  SelectionState selection);
TransformResult IndentLines(std::vector<std::string>& lines,
                            CursorState cursor,
                            SelectionState selection,
                            size_t indent_width);
TransformResult OutdentLines(std::vector<std::string>& lines,
                             CursorState cursor,
                             SelectionState selection,
                             size_t indent_width);
TransformResult ToggleCase(std::vector<std::string>& lines,
                           CursorState cursor,
                           SelectionState selection);

} // namespace textlt::transform
