#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace textlt::utils {

struct Position {
    size_t x = 0;
    size_t y = 0;
};

bool PositionLess(const Position& left, const Position& right);
Position ClampedPosition(Position position, const std::vector<std::string>& lines);
std::pair<Position, Position> OrderedSelection(
    Position anchor,
    Position cursor,
    const std::vector<std::string>& lines);

bool IsOpeningBracket(char character);
bool IsClosingBracket(char character);
bool IsBracketCharacter(char character);
char MatchingBracketFor(char character);

size_t FindWordDeleteStart(const std::string& line, size_t cursor_x);
size_t FindWordDeleteEnd(const std::string& line, size_t cursor_x);

} // namespace textlt::utils
