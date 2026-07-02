#include "document.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "editor_utils.hpp"

namespace textlt {

bool Document::InsertText(const std::string& text) {
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

    // DEBUG
    {
        std::ofstream dbg("/tmp/textlt_doc_insert.log", std::ios::app);
        dbg << "InsertText: text.size=" << text.size()
            << " inserted_lines.size=" << inserted_lines.size()
            << " cursor_row=" << cursor_row << " cursor_col=" << cursor_col
            << " lines_before=" << lines.size() << "\n";
    }

    const size_t insertion_row = cursor_row;
    const std::string suffix = lines[insertion_row].substr(cursor_col);
    lines[insertion_row].erase(cursor_col);
    lines[insertion_row] += inserted_lines.front();

    if (inserted_lines.size() == 1) {
        cursor_col = lines[insertion_row].size();
        lines[insertion_row] += suffix;
    } else {
        inserted_lines.back() += suffix;
        lines.insert(
            lines.begin() + static_cast<std::ptrdiff_t>(insertion_row + 1),
            std::make_move_iterator(inserted_lines.begin() + 1),
            std::make_move_iterator(inserted_lines.end()));
        cursor_row = insertion_row + inserted_lines.size() - 1;
        cursor_col = lines[cursor_row].size() - suffix.size();
    }

    // DEBUG
    {
        std::ofstream dbg("/tmp/textlt_doc_insert.log", std::ios::app);
        dbg << "After insert: lines_after=" << lines.size()
            << " cursor_row=" << cursor_row << "\n";
        // Show lines around where table should be
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            if (l.find("1; 2; 3") != std::string::npos ||
                l.find("6; 7; 7") != std::string::npos ||
                l.find("Существует") != std::string::npos) {
                dbg << "  FOUND LINE " << i << ": [" << l << "]\n";
            }
        }
    }

    is_dirty = true;
    return true;
}

bool Document::InsertCharacter(const std::string& input) {
    if (input.empty()) {
        return false;
    }
    EnsureValidBuffer();
    lines[cursor_row].insert(cursor_col, input);
    cursor_col += input.size();
    is_dirty = true;
    return true;
}

bool Document::InsertPairedCharacter(char opening, char closing) {
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
                std::string next_line = lines[cursor_row].substr(cursor_col);
                lines[cursor_row].erase(cursor_col);
                lines.insert(
                    lines.begin() + static_cast<std::ptrdiff_t>(cursor_row + 1),
                    std::move(next_line));
                ++cursor_row;
                cursor_col = 0;
                continue;
            }
            lines[cursor_row].insert(cursor_col, std::string(1, character));
            ++cursor_col;
        }
        is_dirty = true;
        ClearSelection();
        return true;
    }

    lines[cursor_row].insert(cursor_col, std::string(1, opening) + closing);
    ++cursor_col;
    is_dirty = true;
    ClearSelection();
    return true;
}

bool Document::Backspace() {
    EnsureValidBuffer();
    if (cursor_col > 0) {
        SaveSnapshot();
        const size_t erase_start =
            utils::PreviousUtf8CodepointStart(lines[cursor_row], cursor_col);
        lines[cursor_row].erase(erase_start, cursor_col - erase_start);
        cursor_col = erase_start;
        is_dirty = true;
        return true;
    }
    if (cursor_row > 0) {
        SaveSnapshot();
        cursor_col = lines[cursor_row - 1].size();
        lines[cursor_row - 1] += lines[cursor_row];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(cursor_row));
        --cursor_row;
        is_dirty = true;
        return true;
    }
    return false;
}

bool Document::DeleteForward() {
    EnsureValidBuffer();
    if (cursor_col < lines[cursor_row].size()) {
        SaveSnapshot();
        const size_t erase_end =
            utils::NextUtf8CodepointStart(lines[cursor_row], cursor_col);
        lines[cursor_row].erase(cursor_col, erase_end - cursor_col);
        is_dirty = true;
        return true;
    }
    if (cursor_row + 1 < lines.size()) {
        SaveSnapshot();
        lines[cursor_row] += lines[cursor_row + 1];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(cursor_row + 1));
        is_dirty = true;
        return true;
    }
    return false;
}

bool Document::DeleteWordBackward() {
    EnsureValidBuffer();
    if (cursor_col == 0) {
        if (cursor_row == 0) return false;
        SaveSnapshot();
        cursor_col = lines[cursor_row - 1].size();
        lines[cursor_row - 1] += lines[cursor_row];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(cursor_row));
        --cursor_row;
        is_dirty = true;
        return true;
    }

    const size_t target = utils::FindWordDeleteStart(lines[cursor_row], cursor_col);
    if (target == cursor_col) return false;
    SaveSnapshot();
    lines[cursor_row].erase(target, cursor_col - target);
    cursor_col = target;
    is_dirty = true;
    return true;
}

bool Document::DeleteWordForward() {
    EnsureValidBuffer();
    if (cursor_col >= lines[cursor_row].size()) {
        if (cursor_row + 1 >= lines.size()) return false;
        SaveSnapshot();
        lines[cursor_row] += lines[cursor_row + 1];
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(cursor_row + 1));
        is_dirty = true;
        return true;
    }

    const size_t target = utils::FindWordDeleteEnd(lines[cursor_row], cursor_col);
    if (target == cursor_col) return false;
    SaveSnapshot();
    lines[cursor_row].erase(cursor_col, target - cursor_col);
    is_dirty = true;
    return true;
}

bool Document::DeleteCurrentLine() {
    EnsureValidBuffer();
    if (cursor_row >= lines.size()) {
        return false;
    }
    SaveSnapshot();
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(cursor_row));
    if (lines.empty()) {
        lines.push_back("");
        cursor_row = 0;
    } else if (cursor_row >= lines.size()) {
        cursor_row = lines.size() - 1;
    }
    cursor_col = 0;
    is_dirty = true;
    return true;
}

bool Document::MoveLineUp() {
    EnsureValidBuffer();
    if (lines.size() < 2 || cursor_row == 0) return false;
    SaveSnapshot();
    std::swap(lines[cursor_row], lines[cursor_row - 1]);
    --cursor_row;
    cursor_col = std::min(cursor_col, lines[cursor_row].size());
    is_dirty = true;
    return true;
}

bool Document::MoveLinesUp() {
    EnsureValidBuffer();
    if (lines.size() < 2) return false;

    if (!HasSelection()) {
        return MoveLineUp();
    }

    auto [start, end] = utils::OrderedSelection(
        {selection.anchor_x, selection.anchor_y},
        {cursor_col, cursor_row},
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

    --cursor_row;
    --selection.anchor_y;
    ClampCursor();
    selection.anchor_y = std::min(selection.anchor_y, lines.size() - 1);
    selection.anchor_x = std::min(selection.anchor_x, lines[selection.anchor_y].size());
    selection.active = true;
    is_dirty = true;
    return true;
}

bool Document::MoveLineDown() {
    EnsureValidBuffer();
    if (lines.size() < 2 || cursor_row + 1 >= lines.size()) return false;
    SaveSnapshot();
    std::swap(lines[cursor_row], lines[cursor_row + 1]);
    ++cursor_row;
    cursor_col = std::min(cursor_col, lines[cursor_row].size());
    is_dirty = true;
    return true;
}

bool Document::MoveLinesDown() {
    EnsureValidBuffer();
    if (lines.size() < 2) return false;

    if (!HasSelection()) {
        return MoveLineDown();
    }

    auto [start, end] = utils::OrderedSelection(
        {selection.anchor_x, selection.anchor_y},
        {cursor_col, cursor_row},
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

    ++cursor_row;
    ++selection.anchor_y;
    ClampCursor();
    selection.anchor_y = std::min(selection.anchor_y, lines.size() - 1);
    selection.anchor_x = std::min(selection.anchor_x, lines[selection.anchor_y].size());
    selection.active = true;
    is_dirty = true;
    return true;
}

bool Document::DuplicateLine() {
    EnsureValidBuffer();
    SaveSnapshot();
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(cursor_row + 1), lines[cursor_row]);
    ++cursor_row;
    cursor_col = std::min(cursor_col, lines[cursor_row].size());
    is_dirty = true;
    return true;
}

bool Document::DuplicateLines() {
    EnsureValidBuffer();

    if (!HasSelection()) {
        return DuplicateLine();
    }

    auto [start, end] = utils::OrderedSelection(
        {selection.anchor_x, selection.anchor_y},
        {cursor_col, cursor_row},
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
    selection.active = true;
    is_dirty = true;
    return true;
}

} // namespace textlt
