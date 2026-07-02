#include "editor_utils.hpp"

#include <algorithm>
#include <cctype>

#include "ftxui/screen/string.hpp"

namespace textlt::utils {

bool PositionLess(const Position& left, const Position& right) {
    return left.y < right.y || (left.y == right.y && left.x < right.x);
}

Position ClampedPosition(Position position, const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return {};
    }

    position.y = std::min(position.y, lines.size() - 1);
    position.x = std::min(position.x, lines[position.y].size());
    return position;
}

std::pair<Position, Position> OrderedSelection(
    Position anchor,
    Position cursor,
    const std::vector<std::string>& lines) {
    anchor = ClampedPosition(anchor, lines);
    cursor = ClampedPosition(cursor, lines);
    if (PositionLess(cursor, anchor)) {
        std::swap(anchor, cursor);
    }
    return {anchor, cursor};
}

bool IsOpeningBracket(char character) {
    return character == '(' || character == '[' || character == '{';
}

bool IsClosingBracket(char character) {
    return character == ')' || character == ']' || character == '}';
}

bool IsBracketCharacter(char character) {
    return IsOpeningBracket(character) || IsClosingBracket(character);
}

char MatchingBracketFor(char character) {
    switch (character) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default: return '\0';
    }
}

bool IsWordCharacter(char character) {
    const unsigned char byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) || character == '_' || byte >= 0x80;
}

bool IsUtf8ContinuationByte(char character) {
    return (static_cast<unsigned char>(character) & 0xC0) == 0x80;
}

size_t PreviousUtf8CodepointStart(const std::string& text, size_t index) {
    index = std::min(index, text.size());
    if (index == 0) {
        return 0;
    }

    --index;
    while (index > 0 && IsUtf8ContinuationByte(text[index])) {
        --index;
    }
    return index;
}

size_t NextUtf8CodepointStart(const std::string& text, size_t index) {
    index = std::min(index, text.size());
    if (index >= text.size()) {
        return text.size();
    }

    ++index;
    while (index < text.size() && IsUtf8ContinuationByte(text[index])) {
        ++index;
    }
    return index;
}

size_t Utf8DisplayWidth(const std::string& text, size_t start, size_t end) {
    start = std::min(start, text.size());
    while (start < text.size() && IsUtf8ContinuationByte(text[start])) {
        ++start;
    }
    end = std::min(end, text.size());
    while (end > start && end < text.size() && IsUtf8ContinuationByte(text[end])) {
        --end;
    }
    return static_cast<size_t>(std::max(0, ftxui::string_width(text.substr(start, end - start))));
}

size_t Utf8ByteIndexAtDisplayColumn(
    const std::string& text,
    size_t start,
    size_t display_columns) {
    start = std::min(start, text.size());
    while (start < text.size() && IsUtf8ContinuationByte(text[start])) {
        ++start;
    }

    size_t index = start;
    size_t width = 0;
    while (index < text.size()) {
        const size_t next = NextUtf8CodepointStart(text, index);
        const size_t glyph_width = Utf8DisplayWidth(text, index, next);
        if (width + glyph_width > display_columns) {
            break;
        }
        width += glyph_width;
        index = next;
    }
    return index;
}

std::vector<Utf8WrapSegment> BuildUtf8WrapSegments(
    const std::string& line,
    size_t width) {
    std::vector<Utf8WrapSegment> segments;
    width = std::max<size_t>(1, width);
    if (line.empty()) {
        segments.push_back({});
        return segments;
    }

    size_t start = 0;
    while (start < line.size()) {
        const size_t hard_end = Utf8ByteIndexAtDisplayColumn(line, start, width);
        size_t end = hard_end;
        if (hard_end < line.size()) {
            bool found_whitespace = false;
            for (size_t position = hard_end; position > start;) {
                if (std::isspace(static_cast<unsigned char>(line[position - 1]))) {
                    end = position;
                    found_whitespace = true;
                    break;
                }
                position = PreviousUtf8CodepointStart(line, position);
            }
            if (!found_whitespace) {
                for (size_t position = hard_end; position > start;) {
                    const char left = line[position - 1];
                    const char right = line[position];
                    if (std::isspace(static_cast<unsigned char>(left)) ||
                        std::isspace(static_cast<unsigned char>(right)) ||
                        !(IsWordCharacter(left) && IsWordCharacter(right))) {
                        end = position;
                        break;
                    }
                    position = PreviousUtf8CodepointStart(line, position);
                }
            }
        }
        if (end <= start) {
            end = NextUtf8CodepointStart(line, start);
        }
        segments.push_back({start, end});
        start = end;
    }
    return segments;
}

namespace {

bool IsWhitespaceCharacter(char character) {
    return std::isspace(static_cast<unsigned char>(character));
}

bool IsBoundaryWordCharacter(char character) {
    return IsWordCharacter(character);
}

bool IsSameWordBoundaryClass(char left, char right) {
    return IsBoundaryWordCharacter(left) == IsBoundaryWordCharacter(right);
}

} // namespace

size_t FindWordDeleteStart(const std::string& line, size_t cursor_x) {
    size_t index = std::min(cursor_x, line.size());
    while (index > 0 && IsWhitespaceCharacter(line[index - 1])) {
        --index;
    }
    if (index == 0) {
        return 0;
    }

    const char boundary_character = line[index - 1];
    while (index > 0 &&
           !IsWhitespaceCharacter(line[index - 1]) &&
           IsSameWordBoundaryClass(line[index - 1], boundary_character)) {
        --index;
    }
    return index;
}

size_t FindWordDeleteEnd(const std::string& line, size_t cursor_x) {
    size_t index = std::min(cursor_x, line.size());
    while (index < line.size() && IsWhitespaceCharacter(line[index])) {
        ++index;
    }
    if (index >= line.size()) {
        return line.size();
    }

    const char boundary_character = line[index];
    while (index < line.size() &&
           !IsWhitespaceCharacter(line[index]) &&
           IsSameWordBoundaryClass(line[index], boundary_character)) {
        ++index;
    }
    return index;
}

size_t WordWrapVisualRowAtLine(const std::vector<std::string>& lines,
                               size_t line_index, size_t visible_width) {
    size_t visual = 0;
    for (size_t i = 0; i < line_index && i < lines.size(); ++i) {
        auto segments = BuildUtf8WrapSegments(lines[i], visible_width);
        visual += segments.empty() ? 1 : segments.size();
    }
    return visual;
}

size_t WordWrapLineAtVisualRow(const std::vector<std::string>& lines,
                               size_t target_visual, size_t visible_width) {
    size_t visual = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        auto segments = BuildUtf8WrapSegments(lines[i], visible_width);
        size_t line_rows = segments.empty() ? 1 : segments.size();
        if (visual + line_rows > target_visual) {
            return i;
        }
        visual += line_rows;
    }
    return lines.empty() ? 0 : lines.size() - 1;
}

size_t WordWrapMaxScrollY(const std::vector<std::string>& lines,
                          size_t visible_height, size_t visible_width) {
    if (lines.empty()) return 0;
    size_t visual_rows = 0;
    for (size_t i = lines.size(); i > 0; --i) {
        auto segments = BuildUtf8WrapSegments(lines[i - 1], visible_width);
        visual_rows += segments.empty() ? 1 : segments.size();
        if (visual_rows > visible_height) {
            return i;
        }
    }
    return 0;
}

size_t WordWrapTotalVisualRows(const std::vector<std::string>& lines,
                               size_t visible_width) {
    size_t total = 0;
    for (const auto& line : lines) {
        auto segments = BuildUtf8WrapSegments(line, visible_width);
        total += segments.empty() ? 1 : segments.size();
    }
    return total;
}

} // namespace textlt::utils
