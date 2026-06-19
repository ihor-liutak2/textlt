#include "document.hpp"

#include <algorithm>
#include <cctype>

#include "editor_utils.hpp"

namespace textlt {
namespace {

bool IsBlankLine(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](unsigned char character) {
        return std::isspace(character);
    });
}

} // namespace

void Document::EnsureValidBuffer() {
    if (lines.empty()) {
        lines.push_back("");
    }
    ClampCursor();
}

void Document::ClampCursor() {
    if (lines.empty()) {
        lines.push_back("");
    }
    cursor_row = std::min(cursor_row, lines.size() - 1);
    cursor_col = std::min(cursor_col, lines[cursor_row].size());
}

void Document::SetCursorPosition(size_t row, size_t column) {
    EnsureValidBuffer();
    cursor_row = std::min(row, lines.size() - 1);
    cursor_col = std::min(column, lines[cursor_row].size());
}

void Document::JumpToLine(size_t line_number) {
    EnsureValidBuffer();
    if (line_number < 1) {
        line_number = 1;
    }
    cursor_row = std::min(line_number, lines.size()) - 1;
    cursor_col = 0;
}

void Document::MoveCursorHome() {
    cursor_col = 0;
}

void Document::MoveCursorEnd() {
    ClampCursor();
    cursor_col = lines[cursor_row].size();
}

void Document::MoveCursorLeft() {
    ClampCursor();
    if (cursor_col > 0) {
        cursor_col = utils::PreviousUtf8CodepointStart(lines[cursor_row], cursor_col);
    } else if (cursor_row > 0) {
        --cursor_row;
        cursor_col = lines[cursor_row].size();
    }
}

void Document::MoveCursorRight() {
    ClampCursor();
    if (cursor_col < lines[cursor_row].size()) {
        cursor_col = utils::NextUtf8CodepointStart(lines[cursor_row], cursor_col);
    } else if (cursor_row + 1 < lines.size()) {
        ++cursor_row;
        cursor_col = 0;
    }
}

void Document::MoveCursorUp() {
    ClampCursor();
    if (cursor_row > 0) {
        --cursor_row;
        cursor_col = std::min(cursor_col, lines[cursor_row].size());
    }
}

void Document::MoveCursorDown() {
    ClampCursor();
    if (cursor_row + 1 < lines.size()) {
        ++cursor_row;
        cursor_col = std::min(cursor_col, lines[cursor_row].size());
    }
}

void Document::MoveCursorToPreviousParagraph() {
    ClampCursor();
    if (cursor_row == 0) {
        cursor_col = 0;
        return;
    }

    if (!IsBlankLine(lines[cursor_row])) {
        size_t paragraph_start = cursor_row;
        while (paragraph_start > 0 && !IsBlankLine(lines[paragraph_start - 1])) {
            --paragraph_start;
        }
        if (cursor_row != paragraph_start || cursor_col != 0) {
            cursor_row = paragraph_start;
            cursor_col = 0;
            return;
        }
    }

    size_t target = cursor_row;
    if (target > 0) --target;
    while (target > 0 && IsBlankLine(lines[target])) {
        --target;
    }
    while (target > 0 && !IsBlankLine(lines[target - 1])) {
        --target;
    }
    cursor_row = target;
    cursor_col = 0;
}

void Document::MoveCursorToNextParagraph() {
    ClampCursor();
    size_t target = cursor_row;
    if (!IsBlankLine(lines[target])) {
        while (target + 1 < lines.size() && !IsBlankLine(lines[target + 1])) {
            ++target;
        }
    }
    while (target + 1 < lines.size() && IsBlankLine(lines[target + 1])) {
        ++target;
    }

    if (target + 1 < lines.size()) {
        cursor_row = target + 1;
        cursor_col = 0;
        return;
    }

    cursor_row = lines.size() - 1;
    cursor_col = lines[cursor_row].size();
}

void Document::MoveCursorToPreviousWord() {
    ClampCursor();
    if (cursor_col == 0) {
        if (cursor_row == 0) return;
        --cursor_row;
        cursor_col = lines[cursor_row].size();
    }

    const std::string& line = lines[cursor_row];
    while (cursor_col > 0 && !utils::IsWordCharacter(line[cursor_col - 1])) {
        --cursor_col;
    }
    while (cursor_col > 0 && utils::IsWordCharacter(line[cursor_col - 1])) {
        --cursor_col;
    }
}

void Document::MoveCursorToNextWord() {
    ClampCursor();
    const std::string& line = lines[cursor_row];
    while (cursor_col < line.size() && utils::IsWordCharacter(line[cursor_col])) {
        ++cursor_col;
    }
    while (cursor_col < line.size() && !utils::IsWordCharacter(line[cursor_col])) {
        ++cursor_col;
    }
    if (cursor_col == line.size() && cursor_row + 1 < lines.size()) {
        ++cursor_row;
        cursor_col = 0;
    }
}

} // namespace textlt
