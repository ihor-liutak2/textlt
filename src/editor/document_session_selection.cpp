#include "editor/document_session.hpp"

#include <algorithm>

#include "editor_utils.hpp"

namespace textlt {

bool DocumentSession::HasSelection() const {
    return selection.active &&
        (cursor_col != selection.anchor_x || cursor_row != selection.anchor_y);
}

void DocumentSession::BeginSelection() {
    if (!selection.active) {
        selection.anchor_x = cursor_col;
        selection.anchor_y = cursor_row;
    }
    selection.active = true;
}

void DocumentSession::ClearSelection() {
    ClampCursor();
    selection.active = false;
    selection.anchor_x = cursor_col;
    selection.anchor_y = cursor_row;
}

void DocumentSession::SetSelectionAnchor(size_t row, size_t column) {
    EnsureValidBuffer();
    selection.anchor_y = std::min(row, lines.size() - 1);
    selection.anchor_x = std::min(column, lines[selection.anchor_y].size());
}

void DocumentSession::SetSelectionActive(bool active) {
    selection.active = active;
}

void DocumentSession::SelectAll() {
    EnsureValidBuffer();
    selection.anchor_x = 0;
    selection.anchor_y = 0;
    cursor_row = lines.size() - 1;
    cursor_col = lines[cursor_row].size();
    selection.active = true;
}

std::string DocumentSession::GetSelectedText() const {
    if (!HasSelection()) {
        return "";
    }

    auto [start, end] = utils::OrderedSelection(
        {selection.anchor_x, selection.anchor_y},
        {cursor_col, cursor_row},
        lines);

    if (start.y == end.y) {
        return lines[start.y].substr(start.x, end.x - start.x);
    }

    std::string selected = lines[start.y].substr(start.x);
    selected.push_back('\n');
    for (size_t y = start.y + 1; y < end.y; ++y) {
        selected += lines[y];
        selected.push_back('\n');
    }
    selected += lines[end.y].substr(0, end.x);
    return selected;
}

bool DocumentSession::IsPositionSelected(size_t x, size_t y) const {
    if (!HasSelection()) {
        return false;
    }

    auto [start, end] = utils::OrderedSelection(
        {selection.anchor_x, selection.anchor_y},
        {cursor_col, cursor_row},
        lines);
    const utils::Position position{x, y};
    return !utils::PositionLess(position, start) && utils::PositionLess(position, end);
}

bool DocumentSession::DeleteSelection() {
    if (!HasSelection()) {
        return false;
    }

    EndTypingGroup();
    SaveSnapshot();
    return DeleteSelectionWithoutSnapshot();
}

bool DocumentSession::DeleteSelectionWithoutSnapshot() {
    if (!HasSelection()) {
        return false;
    }

    auto [start, end] = utils::OrderedSelection(
        {selection.anchor_x, selection.anchor_y},
        {cursor_col, cursor_row},
        lines);

    if (start.y == end.y) {
        lines[start.y].erase(start.x, end.x - start.x);
    } else {
        lines[start.y] = lines[start.y].substr(0, start.x) + lines[end.y].substr(end.x);
        lines.erase(
            lines.begin() + static_cast<std::ptrdiff_t>(start.y + 1),
            lines.begin() + static_cast<std::ptrdiff_t>(end.y + 1));
    }

    if (lines.empty()) {
        lines.push_back("");
    }

    cursor_col = start.x;
    cursor_row = start.y;
    ClampCursor();
    ClearSelection();
    buffer.MarkDirty();
    return true;
}

} // namespace textlt
