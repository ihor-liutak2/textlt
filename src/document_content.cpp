#include "document.hpp"

#include <algorithm>

namespace textlt {

void Document::LoadContent(const std::string& content, std::filesystem::path p) {
    size_t lf_count = 0;
    size_t crlf_count = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            ++lf_count;
            if (i > 0 && content[i - 1] == '\r') {
                ++crlf_count;
            }
        }
    }
    line_ending = (lf_count > 0 && crlf_count == lf_count) ? LineEnding::CRLF : LineEnding::LF;

    buffer.SetText(content);
    SetPath(std::move(p));
    cursor_row = 0;
    cursor_col = 0;
    is_dirty = false;
    history.Clear();
    EnsureValidBuffer();
}

std::string Document::ToContent() const {
    const std::string ending = line_ending == LineEnding::CRLF ? "\r\n" : "\n";
    return buffer.ToText(ending);
}

std::string Document::LineEndingLabel() const {
    return session.LineEndingLabel();
}

std::string Document::CurrentFilePath() const {
    return session.CurrentFilePath();
}

size_t Document::LineCount() const {
    return lines.size();
}

std::string Document::CurrentLineText() const {
    if (cursor_row < lines.size()) {
        return lines[cursor_row];
    }
    return "";
}

std::string Document::TextFromCursor() const {
    if (lines.empty()) {
        return "";
    }

    const size_t row = std::min(cursor_row, lines.size() - 1);
    const size_t column = std::min(cursor_col, lines[row].size());
    std::string text = lines[row].substr(column);
    for (size_t line_index = row + 1; line_index < lines.size(); ++line_index) {
        text.push_back('\n');
        text += lines[line_index];
    }
    return text;
}

std::string Document::CommentPrefix() const {
    return session.CommentPrefix();
}

} // namespace textlt
