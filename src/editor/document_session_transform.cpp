#include "editor/document_session.hpp"

#include <algorithm>

namespace textlt {

bool DocumentSession::ConvertTabsToSpaces(size_t tab_size) {
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
    size_t adjusted_cursor_x = CursorCol();
    const std::string spaces(tab_size, ' ');
    for (size_t y = 0; y < lines.size(); ++y) {
        std::string& line = lines[y];
        if (y == CursorRow()) {
            size_t tabs_before_cursor = 0;
            const size_t cursor_limit = std::min(CursorCol(), line.size());
            for (size_t x = 0; x < cursor_limit; ++x) {
                if (line[x] == '\t') {
                    ++tabs_before_cursor;
                }
            }
            adjusted_cursor_x = CursorCol() + tabs_before_cursor * (tab_size - 1);
        }

        size_t tab_position = line.find('\t');
        while (tab_position != std::string::npos) {
            line.replace(tab_position, 1, spaces);
            tab_position = line.find('\t', tab_position + tab_size);
        }
    }

    CursorCol() = std::min(adjusted_cursor_x, lines[CursorRow()].size());
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::Convert4To2Spaces() {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::Convert4To2Spaces(
        lines,
        {CursorCol(), CursorRow()},
        {HasSelection(), SelectionState().anchor_x, SelectionState().anchor_y});
    if (!result.changed) return false;

    history.PushSnapshot(before);
    CursorCol() = result.cursor.x;
    CursorRow() = result.cursor.y;
    SelectionState().anchor_x = result.selection.anchor_x;
    SelectionState().anchor_y = result.selection.anchor_y;
    SelectionState().active = result.selection.active;
    buffer.MarkDirty();
    ClampCursor();
    return true;
}

bool DocumentSession::Convert2To4Spaces() {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::Convert2To4Spaces(
        lines,
        {CursorCol(), CursorRow()},
        {HasSelection(), SelectionState().anchor_x, SelectionState().anchor_y});
    if (!result.changed) return false;

    history.PushSnapshot(before);
    CursorCol() = result.cursor.x;
    CursorRow() = result.cursor.y;
    SelectionState().anchor_x = result.selection.anchor_x;
    SelectionState().anchor_y = result.selection.anchor_y;
    SelectionState().active = result.selection.active;
    buffer.MarkDirty();
    ClampCursor();
    return true;
}

bool DocumentSession::IndentLines(size_t tab_size) {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::IndentLines(
        lines,
        {CursorCol(), CursorRow()},
        {HasSelection(), SelectionState().anchor_x, SelectionState().anchor_y},
        tab_size);
    if (!result.changed) return false;

    history.PushSnapshot(before);
    CursorCol() = result.cursor.x;
    CursorRow() = result.cursor.y;
    SelectionState().anchor_x = result.selection.anchor_x;
    SelectionState().anchor_y = result.selection.anchor_y;
    SelectionState().active = result.selection.active;
    buffer.MarkDirty();
    ClampCursor();
    return true;
}

bool DocumentSession::OutdentLines(size_t tab_size) {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::OutdentLines(
        lines,
        {CursorCol(), CursorRow()},
        {HasSelection(), SelectionState().anchor_x, SelectionState().anchor_y},
        tab_size);
    if (!result.changed) return false;

    history.PushSnapshot(before);
    CursorCol() = result.cursor.x;
    CursorRow() = result.cursor.y;
    SelectionState().anchor_x = result.selection.anchor_x;
    SelectionState().anchor_y = result.selection.anchor_y;
    SelectionState().active = result.selection.active;
    buffer.MarkDirty();
    ClampCursor();
    return true;
}

bool DocumentSession::ToggleCase() {
    EnsureValidBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::ToggleCase(
        lines,
        {CursorCol(), CursorRow()},
        {HasSelection(), SelectionState().anchor_x, SelectionState().anchor_y});
    if (!result.changed) return false;

    history.PushSnapshot(before);
    CursorCol() = result.cursor.x;
    CursorRow() = result.cursor.y;
    SelectionState().anchor_x = result.selection.anchor_x;
    SelectionState().anchor_y = result.selection.anchor_y;
    SelectionState().active = result.selection.active;
    buffer.MarkDirty();
    ClampCursor();
    return true;
}

} // namespace textlt
