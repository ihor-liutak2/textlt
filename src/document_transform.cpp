#include "document.hpp"

#include <algorithm>

namespace textlt {

bool Document::ConvertTabsToSpaces(size_t tab_size) {
    EnsureValidBuffer();
    bool has_tabs = false;
    for (const std::string& line : lines) {
        if (line.find('\t') != std::string::npos) {
            has_tabs = true;
            break;
        }
    }
    if (!has_tabs) {
        return false;
    }

    SaveSnapshot();
    size_t adjusted_cursor_x = cursor_col;
    const std::string spaces(tab_size, ' ');
    for (size_t y = 0; y < lines.size(); ++y) {
        std::string& line = lines[y];
        if (y == cursor_row) {
            size_t tabs_before_cursor = 0;
            const size_t cursor_limit = std::min(cursor_col, line.size());
            for (size_t x = 0; x < cursor_limit; ++x) {
                if (line[x] == '\t') {
                    ++tabs_before_cursor;
                }
            }
            adjusted_cursor_x = cursor_col + tabs_before_cursor * (tab_size - 1);
        }

        size_t tab_position = line.find('\t');
        while (tab_position != std::string::npos) {
            line.replace(tab_position, 1, spaces);
            tab_position = line.find('\t', tab_position + tab_size);
        }
    }

    cursor_col = std::min(adjusted_cursor_x, lines[cursor_row].size());
    is_dirty = true;
    return true;
}

bool Document::Convert4To2Spaces() {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::Convert4To2Spaces(
        lines,
        {cursor_col, cursor_row},
        {HasSelection(), selection.anchor_x, selection.anchor_y});
    if (!result.changed) return false;

    history.PushSnapshot(before);
    cursor_col = result.cursor.x;
    cursor_row = result.cursor.y;
    selection.anchor_x = result.selection.anchor_x;
    selection.anchor_y = result.selection.anchor_y;
    selection.active = result.selection.active;
    is_dirty = true;
    ClampCursor();
    return true;
}

bool Document::Convert2To4Spaces() {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::Convert2To4Spaces(
        lines,
        {cursor_col, cursor_row},
        {HasSelection(), selection.anchor_x, selection.anchor_y});
    if (!result.changed) return false;

    history.PushSnapshot(before);
    cursor_col = result.cursor.x;
    cursor_row = result.cursor.y;
    selection.anchor_x = result.selection.anchor_x;
    selection.anchor_y = result.selection.anchor_y;
    selection.active = result.selection.active;
    is_dirty = true;
    ClampCursor();
    return true;
}

bool Document::ToggleCase() {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::ToggleCase(
        lines,
        {cursor_col, cursor_row},
        {HasSelection(), selection.anchor_x, selection.anchor_y});
    if (!result.changed) return false;

    history.PushSnapshot(before);
    cursor_col = result.cursor.x;
    cursor_row = result.cursor.y;
    selection.anchor_x = result.selection.anchor_x;
    selection.anchor_y = result.selection.anchor_y;
    selection.active = result.selection.active;
    is_dirty = true;
    ClampCursor();
    return true;
}

} // namespace textlt
