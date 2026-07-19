#include "editor/document_session.hpp"

#include <algorithm>

#include "editor_utils.hpp"

namespace textlt {

bool DocumentSession::HasSelection() const {
    return SelectionState().active &&
        (CursorCol() != SelectionState().anchor_x || CursorRow() != SelectionState().anchor_y);
}

bool DocumentSession::SelectionAnchorModeActive() const {
    return CursorState().selection_anchor_mode;
}

void DocumentSession::BeginSelection() {
    if (!HasSelection()) {
        SelectionState().anchor_x = CursorCol();
        SelectionState().anchor_y = CursorRow();
    }
    SelectionState().active = true;
}

void DocumentSession::ClearSelection() {
    ClampCursor();
    CursorState().selection_anchor_mode = false;
    SelectionState().active = false;
    SelectionState().anchor_x = CursorCol();
    SelectionState().anchor_y = CursorRow();
}

void DocumentSession::SetSelectionAnchor(size_t row, size_t column) {
    EnsureValidBuffer();
    SelectionState().anchor_y = std::min(row, lines.size() - 1);
    SelectionState().anchor_x = std::min(column, lines[SelectionState().anchor_y].size());
}

void DocumentSession::SetSelectionActive(bool active) {
    CursorState().selection_anchor_mode = false;
    SelectionState().active = active;
}

void DocumentSession::SelectAll() {
    EnsureValidBuffer();
    CursorState().selection_anchor_mode = false;
    SelectionState().anchor_x = 0;
    SelectionState().anchor_y = 0;
    CursorRow() = lines.size() - 1;
    CursorCol() = lines[CursorRow()].size();
    SelectionState().active = true;
}

void DocumentSession::SelectCurrentLine() {
    EnsureValidBuffer();
    CursorState().selection_anchor_mode = false;
    ClampCursor();
    SelectionState().anchor_y = CursorRow();
    SelectionState().anchor_x = 0;
    CursorCol() = lines[CursorRow()].size();
    SelectionState().active = CursorCol() != 0;
}

void DocumentSession::ToggleSelectionAnchor() {
    if (CursorState().selection_anchor_mode) {
        ClearSelection();
        return;
    }
    ClampCursor();
    SelectionState().anchor_x = CursorCol();
    SelectionState().anchor_y = CursorRow();
    SelectionState().active = true;
    CursorState().selection_anchor_mode = true;
}

std::string DocumentSession::GetSelectedText() const {
    if (!HasSelection()) {
        return "";
    }

    auto [start, end] = utils::OrderedSelection(
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
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
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
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
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
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

    CursorCol() = start.x;
    CursorRow() = start.y;
    ClampCursor();
    ClearSelection();
    buffer.MarkDirty();
    return true;
}

} // namespace textlt
