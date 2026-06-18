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

    lines.clear();
    std::string line;
    for (char character : content) {
        if (character == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
            line.clear();
        } else {
            line.push_back(character);
        }
    }
    if (!content.empty() && content.back() != '\n') {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    SetPath(std::move(p));
    cursor_row = 0;
    cursor_col = 0;
    is_dirty = false;
    history.Clear();
    EnsureValidBuffer();
}

std::string Document::ToContent() const {
    if (lines.empty()) {
        return "";
    }

    const std::string ending = line_ending == LineEnding::CRLF ? "\r\n" : "\n";
    std::string full_text;
    full_text.reserve(lines.size() * 80);
    for (size_t i = 0; i < lines.size(); ++i) {
        full_text += lines[i];
        if (i + 1 < lines.size()) {
            full_text += ending;
        }
    }
    return full_text;
}

std::string Document::LineEndingLabel() const {
    return line_ending == LineEnding::CRLF ? "CRLF" : "LF";
}

std::string Document::CurrentFilePath() const {
    return path.string();
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
    const std::string filename = path.filename().string();
    const std::string extension = path.extension().string();

    if (extension == ".sql" || extension == ".graphql" || extension == ".gql") {
        return "--";
    }

    if (filename.rfind("Dockerfile", 0) == 0 ||
        filename == ".bashrc" || filename == ".profile" ||
        filename == ".env" || filename == ".env.local" ||
        filename == ".env.development" || filename == ".env.production" ||
        (filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".env") == 0) ||
        extension == ".conf" || extension == ".ini" || extension == ".py" ||
        extension == ".rb" || extension == ".yaml" || extension == ".yml" ||
        extension == ".sh" || extension == ".bash" || extension == ".zsh" ||
        extension == ".bashrc" || extension == ".profile") {
        return "#";
    }

    return "//";
}

} // namespace textlt
