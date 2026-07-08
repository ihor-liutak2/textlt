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

struct Utf8WrapSegment {
    size_t start = 0;
    size_t end = 0;
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
bool IsWordCharacter(char character);

bool IsUtf8ContinuationByte(char character);
size_t PreviousUtf8CodepointStart(const std::string& text, size_t index);
size_t NextUtf8CodepointStart(const std::string& text, size_t index);
size_t Utf8DisplayWidth(const std::string& text, size_t start = 0, size_t end = std::string::npos);
size_t Utf8ByteIndexAtDisplayColumn(
    const std::string& text,
    size_t start,
    size_t display_columns);
std::vector<Utf8WrapSegment> BuildUtf8WrapSegments(const std::string& line, size_t width);
std::vector<Utf8WrapSegment> BuildUtf8WrapSegmentsLimited(
    const std::string& line,
    size_t width,
    size_t max_segments);

size_t FindWordDeleteStart(const std::string& line, size_t cursor_x);
size_t FindWordDeleteEnd(const std::string& line, size_t cursor_x);

} // namespace textlt::utils
