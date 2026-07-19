#include "editor/document_session.hpp"

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

void DocumentSession::EnsureValidBuffer() {
    if (lines.empty()) {
        lines.push_back("");
    }
    ClampCursor();
}

void DocumentSession::ClampCursor() {
    if (lines.empty()) {
        lines.push_back("");
    }
    CursorRow() = std::min(CursorRow(), lines.size() - 1);
    CursorCol() = std::min(CursorCol(), lines[CursorRow()].size());
}

void DocumentSession::SetCursorPosition(size_t row, size_t column) {
    EnsureValidBuffer();
    CursorRow() = std::min(row, lines.size() - 1);
    CursorCol() = std::min(column, lines[CursorRow()].size());
}

void DocumentSession::JumpToLine(size_t line_number) {
    EnsureValidBuffer();
    if (line_number < 1) {
        line_number = 1;
    }
    CursorRow() = std::min(line_number, lines.size()) - 1;
    CursorCol() = 0;
}

void DocumentSession::MoveCursorHome() {
    CursorCol() = 0;
}

void DocumentSession::MoveCursorEnd() {
    ClampCursor();
    CursorCol() = lines[CursorRow()].size();
}

void DocumentSession::MoveCursorDocumentStart() {
    EnsureValidBuffer();
    CursorRow() = 0;
    CursorCol() = 0;
}

void DocumentSession::MoveCursorDocumentEnd() {
    EnsureValidBuffer();
    CursorRow() = lines.size() - 1;
    CursorCol() = lines.back().size();
}

void DocumentSession::MoveCursorLeft() {
    ClampCursor();
    if (CursorCol() > 0) {
        CursorCol() = utils::PreviousUtf8CodepointStart(lines[CursorRow()], CursorCol());
    } else if (CursorRow() > 0) {
        --CursorRow();
        CursorCol() = lines[CursorRow()].size();
    }
}

void DocumentSession::MoveCursorRight() {
    ClampCursor();
    if (CursorCol() < lines[CursorRow()].size()) {
        CursorCol() = utils::NextUtf8CodepointStart(lines[CursorRow()], CursorCol());
    } else if (CursorRow() + 1 < lines.size()) {
        ++CursorRow();
        CursorCol() = 0;
    }
}

void DocumentSession::MoveCursorUp() {
    ClampCursor();
    if (CursorRow() > 0) {
        const size_t display_column =
            utils::Utf8DisplayWidth(lines[CursorRow()], 0, CursorCol());
        --CursorRow();
        CursorCol() = utils::Utf8ByteIndexAtDisplayColumn(
            lines[CursorRow()], 0, display_column);
    }
}

void DocumentSession::MoveCursorDown() {
    ClampCursor();
    if (CursorRow() + 1 < lines.size()) {
        const size_t display_column =
            utils::Utf8DisplayWidth(lines[CursorRow()], 0, CursorCol());
        ++CursorRow();
        CursorCol() = utils::Utf8ByteIndexAtDisplayColumn(
            lines[CursorRow()], 0, display_column);
    }
}

void DocumentSession::MoveCursorToPreviousParagraph() {
    ClampCursor();
    if (CursorRow() == 0) {
        CursorCol() = 0;
        return;
    }

    if (!IsBlankLine(lines[CursorRow()])) {
        size_t paragraph_start = CursorRow();
        while (paragraph_start > 0 && !IsBlankLine(lines[paragraph_start - 1])) {
            --paragraph_start;
        }
        if (CursorRow() != paragraph_start || CursorCol() != 0) {
            CursorRow() = paragraph_start;
            CursorCol() = 0;
            return;
        }
    }

    size_t target = CursorRow();
    if (target > 0) --target;
    while (target > 0 && IsBlankLine(lines[target])) {
        --target;
    }
    while (target > 0 && !IsBlankLine(lines[target - 1])) {
        --target;
    }
    CursorRow() = target;
    CursorCol() = 0;
}

void DocumentSession::MoveCursorToNextParagraph() {
    ClampCursor();
    size_t target = CursorRow();
    if (!IsBlankLine(lines[target])) {
        while (target + 1 < lines.size() && !IsBlankLine(lines[target + 1])) {
            ++target;
        }
    }
    while (target + 1 < lines.size() && IsBlankLine(lines[target + 1])) {
        ++target;
    }

    if (target + 1 < lines.size()) {
        CursorRow() = target + 1;
        CursorCol() = 0;
        return;
    }

    CursorRow() = lines.size() - 1;
    CursorCol() = lines[CursorRow()].size();
}

void DocumentSession::MoveCursorToPreviousParagraphSelectionBoundary() {
    MoveCursorToPreviousParagraph();
}

void DocumentSession::MoveCursorToNextParagraphSelectionBoundary() {
    ClampCursor();
    if (lines.empty()) {
        return;
    }

    size_t paragraph_end = CursorRow();
    if (!IsBlankLine(lines[paragraph_end])) {
        while (paragraph_end + 1 < lines.size() && !IsBlankLine(lines[paragraph_end + 1])) {
            ++paragraph_end;
        }
        const size_t end_column = lines[paragraph_end].size();
        if (CursorRow() != paragraph_end || CursorCol() != end_column) {
            CursorRow() = paragraph_end;
            CursorCol() = end_column;
            return;
        }
    }

    size_t next = paragraph_end + 1;
    while (next < lines.size() && IsBlankLine(lines[next])) {
        ++next;
    }
    if (next >= lines.size()) {
        MoveCursorDocumentEnd();
        return;
    }
    while (next + 1 < lines.size() && !IsBlankLine(lines[next + 1])) {
        ++next;
    }
    CursorRow() = next;
    CursorCol() = lines[next].size();
}

void DocumentSession::MoveCursorToPreviousWord() {
    ClampCursor();
    if (CursorCol() == 0) {
        if (CursorRow() == 0) return;
        --CursorRow();
        CursorCol() = lines[CursorRow()].size();
    }

    const std::string& line = lines[CursorRow()];
    while (CursorCol() > 0 && !utils::IsWordCharacter(line[CursorCol() - 1])) {
        --CursorCol();
    }
    while (CursorCol() > 0 && utils::IsWordCharacter(line[CursorCol() - 1])) {
        --CursorCol();
    }
}

void DocumentSession::MoveCursorToNextWord() {
    ClampCursor();
    const std::string& line = lines[CursorRow()];
    while (CursorCol() < line.size() && utils::IsWordCharacter(line[CursorCol()])) {
        ++CursorCol();
    }
    while (CursorCol() < line.size() && !utils::IsWordCharacter(line[CursorCol()])) {
        ++CursorCol();
    }
    if (CursorCol() == line.size() && CursorRow() + 1 < lines.size()) {
        ++CursorRow();
        CursorCol() = 0;
    }
}

} // namespace textlt
