#include "editor_utils.hpp"

#include <algorithm>
#include <cctype>

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
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
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

} // namespace textlt::utils
