#include "editor/document_session.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "editor_utils.hpp"

namespace textlt {

bool DocumentSession::InsertText(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    EnsureValidBuffer();
    SaveSnapshot();
    if (HasSelection()) {
        DeleteSelectionWithoutSnapshot();
    }

    std::vector<std::string> inserted_lines(1);
    for (char character : text) {
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            inserted_lines.emplace_back();
        } else {
            inserted_lines.back().push_back(character);
        }
    }

    const size_t insertion_row = CursorRow();
    const std::string suffix = lines[insertion_row].substr(CursorCol());
    lines[insertion_row].erase(CursorCol());
    lines[insertion_row] += inserted_lines.front();

    if (inserted_lines.size() == 1) {
        CursorCol() = lines[insertion_row].size();
        lines[insertion_row] += suffix;
    } else {
        inserted_lines.back() += suffix;
        lines.insert(
            lines.begin() + static_cast<std::ptrdiff_t>(insertion_row + 1),
            std::make_move_iterator(inserted_lines.begin() + 1),
            std::make_move_iterator(inserted_lines.end()));
        CursorRow() = insertion_row + inserted_lines.size() - 1;
        CursorCol() = lines[CursorRow()].size() - suffix.size();
    }

    buffer.MarkDirty();
    return true;
}

bool DocumentSession::InsertCharacter(const std::string& input) {
    if (input.empty()) {
        return false;
    }
    EnsureValidBuffer();
    lines[CursorRow()].insert(CursorCol(), input);
    CursorCol() += input.size();
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::InsertPairedCharacter(char opening, char closing) {
    EnsureValidBuffer();
    EndTypingGroup();
    SaveSnapshot();

    if (HasSelection()) {
        const std::string selected = GetSelectedText();
        DeleteSelectionWithoutSnapshot();
        const std::string wrapped = std::string(1, opening) + selected + closing;
        for (char character : wrapped) {
            if (character == '\r') {
                continue;
            }
            if (character == '\n') {
                std::string next_line = lines[CursorRow()].substr(CursorCol());
                lines[CursorRow()].erase(CursorCol());
                lines.insert(
                    lines.begin() + static_cast<std::ptrdiff_t>(CursorRow() + 1),
                    std::move(next_line));
                ++CursorRow();
                CursorCol() = 0;
                continue;
            }
            lines[CursorRow()].insert(CursorCol(), std::string(1, character));
            ++CursorCol();
        }
        buffer.MarkDirty();
        ClearSelection();
        return true;
    }

    lines[CursorRow()].insert(CursorCol(), std::string(1, opening) + closing);
    ++CursorCol();
    buffer.MarkDirty();
    ClearSelection();
    return true;
}

bool DocumentSession::Backspace() {
    EnsureValidBuffer();
    if (CursorCol() > 0) {
        SaveSnapshot();
        const size_t erase_start =
            utils::PreviousUtf8CodepointStart(lines[CursorRow()], CursorCol());
        lines[CursorRow()].erase(erase_start, CursorCol() - erase_start);
        CursorCol() = erase_start;
        buffer.MarkDirty();
        return true;
    }
    if (CursorRow() > 0) {
        SaveSnapshot();
        CursorCol() = lines[CursorRow() - 1].size();
        lines[CursorRow() - 1] += lines[CursorRow()];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(CursorRow()));
        --CursorRow();
        buffer.MarkDirty();
        return true;
    }
    return false;
}

bool DocumentSession::DeleteForward() {
    EnsureValidBuffer();
    if (CursorCol() < lines[CursorRow()].size()) {
        SaveSnapshot();
        const size_t erase_end =
            utils::NextUtf8CodepointStart(lines[CursorRow()], CursorCol());
        lines[CursorRow()].erase(CursorCol(), erase_end - CursorCol());
        buffer.MarkDirty();
        return true;
    }
    if (CursorRow() + 1 < lines.size()) {
        SaveSnapshot();
        lines[CursorRow()] += lines[CursorRow() + 1];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(CursorRow() + 1));
        buffer.MarkDirty();
        return true;
    }
    return false;
}

bool DocumentSession::DeleteWordBackward() {
    EnsureValidBuffer();
    if (CursorCol() == 0) {
        if (CursorRow() == 0) return false;
        SaveSnapshot();
        CursorCol() = lines[CursorRow() - 1].size();
        lines[CursorRow() - 1] += lines[CursorRow()];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(CursorRow()));
        --CursorRow();
        buffer.MarkDirty();
        return true;
    }

    const size_t target = utils::FindWordDeleteStart(lines[CursorRow()], CursorCol());
    if (target == CursorCol()) return false;
    SaveSnapshot();
    lines[CursorRow()].erase(target, CursorCol() - target);
    CursorCol() = target;
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::DeleteWordForward() {
    EnsureValidBuffer();
    if (CursorCol() >= lines[CursorRow()].size()) {
        if (CursorRow() + 1 >= lines.size()) return false;
        SaveSnapshot();
        lines[CursorRow()] += lines[CursorRow() + 1];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(CursorRow() + 1));
        buffer.MarkDirty();
        return true;
    }

    const size_t target = utils::FindWordDeleteEnd(lines[CursorRow()], CursorCol());
    if (target == CursorCol()) return false;
    SaveSnapshot();
    lines[CursorRow()].erase(CursorCol(), target - CursorCol());
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::DeleteCurrentLine() {
    EnsureValidBuffer();
    if (CursorRow() >= lines.size()) {
        return false;
    }
    SaveSnapshot();
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(CursorRow()));
    if (lines.empty()) {
        lines.push_back("");
        CursorRow() = 0;
    } else if (CursorRow() >= lines.size()) {
        CursorRow() = lines.size() - 1;
    }
    CursorCol() = 0;
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::MoveLineUp() {
    EnsureValidBuffer();
    if (lines.size() < 2 || CursorRow() == 0) return false;
    SaveSnapshot();
    std::swap(lines[CursorRow()], lines[CursorRow() - 1]);
    --CursorRow();
    CursorCol() = std::min(CursorCol(), lines[CursorRow()].size());
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::MoveLinesUp() {
    EnsureValidBuffer();
    if (lines.size() < 2) return false;

    if (!HasSelection()) {
        return MoveLineUp();
    }

    auto [start, end] = utils::OrderedSelection(
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
        lines);

    size_t start_row = start.y;
    size_t end_row = end.y;
    if (end.x == 0 && end_row > start_row) --end_row;
    if (start_row == 0) return false;

    SaveSnapshot();
    std::rotate(
        lines.begin() + static_cast<std::ptrdiff_t>(start_row - 1),
        lines.begin() + static_cast<std::ptrdiff_t>(start_row),
        lines.begin() + static_cast<std::ptrdiff_t>(end_row + 1));

    --CursorRow();
    --SelectionState().anchor_y;
    ClampCursor();
    SelectionState().anchor_y = std::min(SelectionState().anchor_y, lines.size() - 1);
    SelectionState().anchor_x = std::min(SelectionState().anchor_x, lines[SelectionState().anchor_y].size());
    SelectionState().active = true;
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::MoveLineDown() {
    EnsureValidBuffer();
    if (lines.size() < 2 || CursorRow() + 1 >= lines.size()) return false;
    SaveSnapshot();
    std::swap(lines[CursorRow()], lines[CursorRow() + 1]);
    ++CursorRow();
    CursorCol() = std::min(CursorCol(), lines[CursorRow()].size());
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::MoveLinesDown() {
    EnsureValidBuffer();
    if (lines.size() < 2) return false;

    if (!HasSelection()) {
        return MoveLineDown();
    }

    auto [start, end] = utils::OrderedSelection(
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
        lines);

    size_t start_row = start.y;
    size_t end_row = end.y;
    if (end.x == 0 && end_row > start_row) --end_row;
    if (end_row + 1 >= lines.size()) return false;

    SaveSnapshot();
    std::rotate(
        lines.begin() + static_cast<std::ptrdiff_t>(start_row),
        lines.begin() + static_cast<std::ptrdiff_t>(end_row + 1),
        lines.begin() + static_cast<std::ptrdiff_t>(end_row + 2));

    ++CursorRow();
    ++SelectionState().anchor_y;
    ClampCursor();
    SelectionState().anchor_y = std::min(SelectionState().anchor_y, lines.size() - 1);
    SelectionState().anchor_x = std::min(SelectionState().anchor_x, lines[SelectionState().anchor_y].size());
    SelectionState().active = true;
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::DuplicateLine() {
    EnsureValidBuffer();
    SaveSnapshot();
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(CursorRow() + 1), lines[CursorRow()]);
    ++CursorRow();
    CursorCol() = std::min(CursorCol(), lines[CursorRow()].size());
    buffer.MarkDirty();
    return true;
}

bool DocumentSession::DuplicateLines() {
    EnsureValidBuffer();

    if (!HasSelection()) {
        return DuplicateLine();
    }

    auto [start, end] = utils::OrderedSelection(
        {SelectionState().anchor_x, SelectionState().anchor_y},
        {CursorCol(), CursorRow()},
        lines);

    size_t start_row = start.y;
    size_t end_row = end.y;
    if (end.x == 0 && end_row > start_row) --end_row;
    if (start_row >= lines.size() || end_row >= lines.size() || start_row > end_row) {
        return false;
    }

    SaveSnapshot();
    const auto first = lines.begin() + static_cast<std::ptrdiff_t>(start_row);
    const auto last = lines.begin() + static_cast<std::ptrdiff_t>(end_row + 1);
    std::vector<std::string> copied_lines(first, last);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(end_row + 1),
                 copied_lines.begin(), copied_lines.end());
    SelectionState().active = true;
    buffer.MarkDirty();
    return true;
}

} // namespace textlt
