#include "editor/document_session.hpp"

#include <algorithm>
#include <cctype>

namespace textlt {
namespace {

bool IsBlankLine(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
}

std::string TextInRange(
    const std::vector<std::string>& lines,
    size_t start_row,
    size_t start_column,
    size_t end_row,
    size_t end_column,
    const std::string& line_ending) {
    if (lines.empty() || start_row >= lines.size() || end_row >= lines.size() ||
        start_row > end_row || start_column > lines[start_row].size() ||
        end_column > lines[end_row].size()) {
        return {};
    }
    if (start_row == end_row) {
        if (start_column > end_column) {
            return {};
        }
        return lines[start_row].substr(start_column, end_column - start_column);
    }

    std::string text = lines[start_row].substr(start_column);
    for (size_t row = start_row + 1; row < end_row; ++row) {
        text += line_ending;
        text += lines[row];
    }
    text += line_ending;
    text += lines[end_row].substr(0, end_column);
    return text;
}

} // namespace

bool DocumentSession::CaptureAiTransformTarget(
    bool whole_document,
    DocumentTransformTarget& target,
    std::string& error) const {
    return CaptureAiTransformTargetAt(
        CursorRow(), CursorCol(), whole_document, target, error);
}

bool DocumentSession::CaptureAiTransformTargetAt(
    size_t cursor_row,
    size_t cursor_column,
    bool whole_document,
    DocumentTransformTarget& target,
    std::string& error) const {
    if (buffer.LineCount() == 0) {
        error = "The active document is empty.";
        return false;
    }

    target = {};
    target.buffer_version = buffer.Version();
    target.whole_document = whole_document;
    if (whole_document) {
        target.start_row = 0;
        target.start_column = 0;
        target.end_row = buffer.LineCount() - 1;
        target.end_column = buffer.Line(target.end_row).size();
        target.original_text = buffer.ToText(LineEndingText());
        if (target.original_text.empty()) {
            error = "The active document is empty.";
            return false;
        }
        return true;
    }

    cursor_row = std::min(cursor_row, buffer.LineCount() - 1);
    cursor_column = std::min(cursor_column, buffer.Line(cursor_row).size());
    (void)cursor_column;
    if (IsBlankLine(buffer.Line(cursor_row))) {
        error = "The cursor is on an empty paragraph.";
        return false;
    }

    // Imported and normalized TextLT documents store one paragraph in one
    // logical buffer line. Visual wrapping is handled by the editor viewport,
    // so adjacent non-empty logical lines must not be merged into one AI target.
    target.start_row = cursor_row;
    target.start_column = 0;
    target.end_row = cursor_row;
    target.end_column = buffer.Line(cursor_row).size();
    target.original_text = TextInRange(
        buffer.Lines(),
        target.start_row,
        target.start_column,
        target.end_row,
        target.end_column,
        LineEndingText());
    if (target.original_text.empty()) {
        error = "The current paragraph is empty.";
        return false;
    }
    return true;
}

bool DocumentSession::ReplaceAiTransformTarget(
    const DocumentTransformTarget& target,
    const std::string& text,
    std::string& error) {
    if (read_only) {
        error = "Document is read-only.";
        return false;
    }
    EnsureValidBuffer();
    if (buffer.Version() != target.buffer_version) {
        error = "Document changed while the AI request was running. The result was not applied.";
        return false;
    }
    if (target.start_row >= lines.size() || target.end_row >= lines.size() ||
        target.start_row > target.end_row ||
        target.start_column > lines[target.start_row].size() ||
        target.end_column > lines[target.end_row].size()) {
        error = "The original text range is no longer valid.";
        return false;
    }

    const std::string current_text = TextInRange(
        lines,
        target.start_row,
        target.start_column,
        target.end_row,
        target.end_column,
        LineEndingText());
    if (current_text != target.original_text) {
        error = "Document changed while the AI request was running. The result was not applied.";
        return false;
    }
    if (text == current_text) {
        error = "AI returned the original text without changes.";
        return false;
    }

    history.EndTypingGroup();
    const HistoryManager::State before = CurrentState();

    SelectionState().anchor_x = target.start_column;
    SelectionState().anchor_y = target.start_row;
    SelectionState().active = true;
    CursorState().selection_anchor_mode = false;
    CursorRow() = target.end_row;
    CursorCol() = target.end_column;

    bool changed = DeleteTextProcessorSelection();
    if (!text.empty()) {
        changed = InsertTextProcessorText(text) || changed;
    }
    if (!changed) {
        error = "The AI result could not be inserted.";
        return false;
    }

    history.PushSnapshot(before);
    buffer.MarkDirty();
    ClampCursor();
    return true;
}

} // namespace textlt
