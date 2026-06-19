#include "text_transformer.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace textlt::transform {
namespace {

struct Position {
    size_t x = 0;
    size_t y = 0;
};

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

std::pair<Position, Position> OrderedSelection(Position anchor,
                                               Position cursor,
                                               const std::vector<std::string>& lines) {
    anchor = ClampedPosition(anchor, lines);
    cursor = ClampedPosition(cursor, lines);
    if (PositionLess(cursor, anchor)) {
        std::swap(anchor, cursor);
    }
    return {anchor, cursor};
}

bool IsWordCharacter(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

void ToggleCharacterCase(char& character) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (std::islower(value)) {
        character = static_cast<char>(std::toupper(value));
    } else if (std::isupper(value)) {
        character = static_cast<char>(std::tolower(value));
    }
}

TransformResult ConvertLeadingIndent(std::vector<std::string>& lines,
                                     CursorState cursor,
                                     SelectionState selection,
                                     size_t from_width,
                                     size_t to_width) {
    TransformResult result{false, cursor, selection};
    if (lines.empty() || from_width == 0 || to_width == 0) {
        return result;
    }

    result.cursor.y = std::min(result.cursor.y, lines.size() - 1);
    result.cursor.x = std::min(result.cursor.x, lines[result.cursor.y].size());

    size_t start_row = 0;
    size_t end_row = lines.size() - 1;
    if (selection.active) {
        auto [start, end] = OrderedSelection(
            {selection.anchor_x, selection.anchor_y},
            {result.cursor.x, result.cursor.y},
            lines);
        start_row = start.y;
        end_row = end.y;
        if (end.x == 0 && end_row > start_row) {
            --end_row;
        }
    }

    for (size_t row = start_row; row <= end_row && row < lines.size(); ++row) {
        std::string& line = lines[row];
        size_t leading_spaces = 0;
        while (leading_spaces < line.size() && line[leading_spaces] == ' ') {
            ++leading_spaces;
        }

        const size_t converted_blocks = leading_spaces / from_width;
        if (converted_blocks == 0) {
            continue;
        }

        const size_t remainder = leading_spaces % from_width;
        const size_t new_leading_spaces = converted_blocks * to_width + remainder;
        if (new_leading_spaces == leading_spaces) {
            continue;
        }

        line = std::string(new_leading_spaces, ' ') + line.substr(leading_spaces);
        result.changed = true;
        if (row == result.cursor.y && result.cursor.x <= leading_spaces) {
            result.cursor.x = std::min(result.cursor.x, new_leading_spaces);
        } else if (row == result.cursor.y && result.cursor.x > leading_spaces) {
            result.cursor.x = result.cursor.x + new_leading_spaces - leading_spaces;
        }
    }

    if (result.cursor.y < lines.size()) {
        result.cursor.x = std::min(result.cursor.x, lines[result.cursor.y].size());
    }
    return result;
}

std::pair<size_t, size_t> SelectedLineRange(const std::vector<std::string>& lines,
                                            CursorState cursor,
                                            SelectionState selection) {
    if (lines.empty()) {
        return {0, 0};
    }

    cursor.y = std::min(cursor.y, lines.size() - 1);
    cursor.x = std::min(cursor.x, lines[cursor.y].size());
    if (!selection.active) {
        return {cursor.y, cursor.y};
    }

    auto [start, end] = OrderedSelection(
        {selection.anchor_x, selection.anchor_y},
        {cursor.x, cursor.y},
        lines);
    size_t end_row = end.y;
    if (end.x == 0 && end_row > start.y) {
        --end_row;
    }
    return {start.y, end_row};
}

void AdjustColumnAfterLinePrefixInsert(size_t row,
                                       size_t changed_row,
                                       size_t inserted,
                                       size_t& column) {
    if (row == changed_row) {
        column += inserted;
    }
}

void AdjustColumnAfterLinePrefixErase(size_t row,
                                      size_t changed_row,
                                      size_t erased,
                                      size_t& column) {
    if (row == changed_row) {
        column -= std::min(column, erased);
    }
}

} // namespace

TransformResult Convert4To2Spaces(std::vector<std::string>& lines,
                                  CursorState cursor,
                                  SelectionState selection) {
    return ConvertLeadingIndent(lines, cursor, selection, 4, 2);
}

TransformResult Convert2To4Spaces(std::vector<std::string>& lines,
                                  CursorState cursor,
                                  SelectionState selection) {
    return ConvertLeadingIndent(lines, cursor, selection, 2, 4);
}

TransformResult IndentLines(std::vector<std::string>& lines,
                            CursorState cursor,
                            SelectionState selection,
                            size_t indent_width) {
    TransformResult result{false, cursor, selection};
    if (lines.empty() || indent_width == 0) {
        return result;
    }

    result.cursor.y = std::min(result.cursor.y, lines.size() - 1);
    result.cursor.x = std::min(result.cursor.x, lines[result.cursor.y].size());
    result.selection.anchor_y = std::min(result.selection.anchor_y, lines.size() - 1);
    result.selection.anchor_x =
        std::min(result.selection.anchor_x, lines[result.selection.anchor_y].size());

    const auto [start_row, end_row] = SelectedLineRange(lines, result.cursor, selection);
    const std::string indent(indent_width, ' ');
    for (size_t row = start_row; row <= end_row && row < lines.size(); ++row) {
        lines[row].insert(0, indent);
        AdjustColumnAfterLinePrefixInsert(result.cursor.y, row, indent_width, result.cursor.x);
        if (result.selection.active) {
            AdjustColumnAfterLinePrefixInsert(
                result.selection.anchor_y,
                row,
                indent_width,
                result.selection.anchor_x);
        }
        result.changed = true;
    }

    return result;
}

TransformResult OutdentLines(std::vector<std::string>& lines,
                             CursorState cursor,
                             SelectionState selection,
                             size_t indent_width) {
    TransformResult result{false, cursor, selection};
    if (lines.empty() || indent_width == 0) {
        return result;
    }

    result.cursor.y = std::min(result.cursor.y, lines.size() - 1);
    result.cursor.x = std::min(result.cursor.x, lines[result.cursor.y].size());
    result.selection.anchor_y = std::min(result.selection.anchor_y, lines.size() - 1);
    result.selection.anchor_x =
        std::min(result.selection.anchor_x, lines[result.selection.anchor_y].size());

    const auto [start_row, end_row] = SelectedLineRange(lines, result.cursor, selection);
    for (size_t row = start_row; row <= end_row && row < lines.size(); ++row) {
        std::string& line = lines[row];
        size_t erase_count = 0;
        if (!line.empty() && line.front() == '\t') {
            erase_count = 1;
        } else {
            while (erase_count < indent_width &&
                   erase_count < line.size() &&
                   line[erase_count] == ' ') {
                ++erase_count;
            }
        }
        if (erase_count == 0) {
            continue;
        }

        line.erase(0, erase_count);
        AdjustColumnAfterLinePrefixErase(result.cursor.y, row, erase_count, result.cursor.x);
        if (result.selection.active) {
            AdjustColumnAfterLinePrefixErase(
                result.selection.anchor_y,
                row,
                erase_count,
                result.selection.anchor_x);
        }
        result.changed = true;
    }

    return result;
}

TransformResult ToggleCase(std::vector<std::string>& lines,
                           CursorState cursor,
                           SelectionState selection) {
    TransformResult result{false, cursor, selection};
    if (lines.empty()) {
        return result;
    }

    result.cursor.y = std::min(result.cursor.y, lines.size() - 1);
    result.cursor.x = std::min(result.cursor.x, lines[result.cursor.y].size());

    if (selection.active) {
        auto [start, end] = OrderedSelection(
            {selection.anchor_x, selection.anchor_y},
            {result.cursor.x, result.cursor.y},
            lines);

        for (size_t y = start.y; y <= end.y; ++y) {
            const size_t x_start = y == start.y ? start.x : 0;
            const size_t x_end = y == end.y ? end.x : lines[y].size();
            for (size_t x = x_start; x < x_end && x < lines[y].size(); ++x) {
                ToggleCharacterCase(lines[y][x]);
                result.changed = true;
            }
        }
        return result;
    }

    std::string& line = lines[result.cursor.y];
    if (line.empty()) {
        return result;
    }

    size_t target = result.cursor.x;
    if (target >= line.size() || !IsWordCharacter(line[target])) {
        if (target == 0 || !IsWordCharacter(line[target - 1])) {
            return result;
        }
        --target;
    }

    size_t start = target;
    while (start > 0 && IsWordCharacter(line[start - 1])) {
        --start;
    }
    size_t end = target;
    while (end < line.size() && IsWordCharacter(line[end])) {
        ++end;
    }

    for (size_t x = start; x < end; ++x) {
        ToggleCharacterCase(line[x]);
        result.changed = true;
    }
    return result;
}

} // namespace textlt::transform
