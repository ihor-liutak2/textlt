#include "notes/markdown_importer.hpp"

#include <algorithm>
#include <fstream>
#include <string_view>
#include <utility>

namespace textlt::notes {
namespace {

bool IsSpace(char character) {
    return character == ' ' || character == '\t';
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && IsSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && IsSpace(text.back())) text.remove_suffix(1);
    return text;
}

size_t MarkerLength(std::string_view text, char marker) {
    const size_t count = text.find_first_not_of(marker);
    return count == std::string_view::npos ? text.size() : count;
}

size_t ListMarkerLength(std::string_view text, NoteBlockType& type) {
    if (text.find_first_of("-+*") == 0) {
        type = NoteBlockType::BulletItem;
        return text.size() == 1 || IsSpace(text[1]) ? 1 : 0;
    }
    const size_t digits = text.find_first_not_of("0123456789");
    if (digits == 0 || digits > 9 || digits >= text.size() ||
        (text[digits] != '.' && text[digits] != ')') ||
        (digits + 1 < text.size() && !IsSpace(text[digits + 1]))) {
        return 0;
    }
    type = NoteBlockType::NumberedItem;
    return digits + 1;
}

bool IsThematicBreak(std::string_view text) {
    if (text.empty() || text.find_first_of("-*_") != 0) return false;
    const char marker = text.front();
    size_t count = 0;
    for (const char character : text) {
        if (character == marker) ++count;
        else if (!IsSpace(character)) return false;
    }
    return count >= 3;
}

} // namespace

bool ReadMarkdownNote(const std::filesystem::path& path,
    std::vector<NoteBlock>& blocks, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open the Markdown file.";
        return false;
    }

    std::vector<NoteBlock> parsed;
    std::vector<size_t> list_columns;
    char fence = '\0';
    size_t fence_length = 0;
    bool first_line = true;
    bool can_make_setext = false;
    for (std::string line; std::getline(input, line);) {
        if (first_line && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }
        first_line = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find('\0') != std::string::npos) {
            error = "The Markdown file contains binary data.";
            return false;
        }

        size_t leading = 0;
        size_t columns = 0;
        while (leading < line.size() && IsSpace(line[leading])) {
            columns += line[leading++] == '\t' ? 4 - columns % 4 : 1;
        }
        const std::string_view text = Trim(std::string_view(line).substr(leading));
        const bool setext = can_make_setext && columns < 4 && !text.empty() &&
            (text.front() == '=' || text.front() == '-') &&
            MarkerLength(text, text.front()) == text.size();
        can_make_setext = false;
        if (setext) {
            parsed.back().heading = text.front() == '=' ? 1 : 2;
            continue;
        }

        NoteBlock block;
        std::string_view content = line;
        if (fence != '\0') {
            if (columns < 4 && MarkerLength(text, fence) >= fence_length &&
                MarkerLength(text, fence) == text.size()) {
                fence = '\0';
            }
        } else if (columns < 4 && !text.empty() &&
            (text.front() == '`' || text.front() == '~') &&
            MarkerLength(text, text.front()) >= 3) {
            fence = text.front();
            fence_length = MarkerLength(text, fence);
            list_columns.clear();
        } else {
            const size_t hashes = MarkerLength(text, '#');
            NoteBlockType list_type = NoteBlockType::Paragraph;
            const size_t marker = ListMarkerLength(text, list_type);
            if (columns < 4 && hashes >= 1 && hashes <= 6 &&
                (hashes == text.size() || IsSpace(text[hashes]))) {
                block.heading = static_cast<int>(std::min<size_t>(hashes, 3));
                content = Trim(text.substr(hashes));
                const size_t last = content.find_last_not_of('#');
                if (last == std::string_view::npos) {
                    content = {};
                } else if (last + 1 < content.size() &&
                    IsSpace(content[last])) {
                    content = Trim(content.substr(0, last));
                }
                list_columns.clear();
            } else if ((columns < 4 || !list_columns.empty()) &&
                marker != 0 && !IsThematicBreak(text)) {
                while (!list_columns.empty() && list_columns.back() > columns) {
                    list_columns.pop_back();
                }
                if (list_columns.empty() || list_columns.back() < columns) {
                    list_columns.push_back(columns);
                }
                block.type = list_type;
                block.indent = static_cast<int>(std::min<size_t>(list_columns.size() - 1, 6));
                content = Trim(text.substr(marker));
                if (content.size() >= 3 && content[0] == '[' && content[2] == ']' &&
                    (content[1] == ' ' || content[1] == 'x' || content[1] == 'X') &&
                    (content.size() == 3 || IsSpace(content[3]))) {
                    block.type = NoteBlockType::CheckItem;
                    block.checked = content[1] != ' ';
                    content = Trim(content.substr(3));
                }
            } else if (!text.empty() && !list_columns.empty() &&
                columns > list_columns.back() &&
                parsed.back().type != NoteBlockType::Paragraph) {
                auto& item = parsed.back().runs.front().text;
                if (!item.empty()) item += ' ';
                item.append(text);
                continue;
            } else if (!text.empty()) {
                list_columns.clear();
                can_make_setext = columns < 4 && !IsThematicBreak(text);
            }
        }
        block.id = GenerateUuid();
        block.runs.push_back({std::string(content)});
        parsed.push_back(std::move(block));
    }
    if (input.bad()) {
        error = "Could not read the Markdown file.";
        return false;
    }
    if (parsed.empty()) {
        error = "The Markdown file is empty.";
        return false;
    }
    blocks = std::move(parsed);
    return true;
}

} // namespace textlt::notes
