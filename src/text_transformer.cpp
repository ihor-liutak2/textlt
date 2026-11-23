#include "text_transformer.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>
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

bool DecodeUtf8At(const std::string& value,
                  size_t offset,
                  std::uint32_t& codepoint,
                  size_t& length) {
    if (offset >= value.size()) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(value[offset]);
    if (first < 0x80) {
        codepoint = first;
        length = 1;
        return true;
    }

    size_t expected = 0;
    if ((first & 0xE0) == 0xC0) {
        codepoint = first & 0x1F;
        expected = 2;
    } else if ((first & 0xF0) == 0xE0) {
        codepoint = first & 0x0F;
        expected = 3;
    } else if ((first & 0xF8) == 0xF0) {
        codepoint = first & 0x07;
        expected = 4;
    } else {
        codepoint = first;
        length = 1;
        return false;
    }

    if (offset + expected > value.size()) {
        codepoint = first;
        length = 1;
        return false;
    }

    for (size_t index = 1; index < expected; ++index) {
        const unsigned char next = static_cast<unsigned char>(value[offset + index]);
        if ((next & 0xC0) != 0x80) {
            codepoint = first;
            length = 1;
            return false;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
    }

    constexpr std::uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
    if (codepoint < minimum[expected] || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        length = 1;
        return false;
    }
    length = expected;
    return true;
}

std::string EncodeUtf8(std::uint32_t codepoint) {
    std::string output;
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return output;
}

bool IsAsciiLower(std::uint32_t codepoint) {
    return codepoint >= 'a' && codepoint <= 'z';
}

bool IsAsciiUpper(std::uint32_t codepoint) {
    return codepoint >= 'A' && codepoint <= 'Z';
}

bool IsCyrillicLower(std::uint32_t codepoint) {
    return (codepoint >= 0x0430 && codepoint <= 0x044F) ||
           codepoint == 0x0451 || codepoint == 0x0454 ||
           codepoint == 0x0456 || codepoint == 0x0457 ||
           codepoint == 0x0491;
}

bool IsCyrillicUpper(std::uint32_t codepoint) {
    return (codepoint >= 0x0410 && codepoint <= 0x042F) ||
           codepoint == 0x0401 || codepoint == 0x0404 ||
           codepoint == 0x0406 || codepoint == 0x0407 ||
           codepoint == 0x0490;
}

bool IsUnicodeWordCharacter(std::uint32_t codepoint) {
    return codepoint == '_' ||
           (codepoint >= '0' && codepoint <= '9') ||
           IsAsciiLower(codepoint) || IsAsciiUpper(codepoint) ||
           IsCyrillicLower(codepoint) || IsCyrillicUpper(codepoint);
}

struct CaseRange {
    std::uint32_t first;
    std::uint32_t last;
    std::uint32_t stride;
    int offset;
};

struct SpecialCase {
    std::uint32_t codepoint;
    std::string_view replacement;
};

#include "unicode_case_mappings.inc"

template <size_t RangeCount, size_t SpecialCount>
bool AppendCaseMapping(std::string& output, std::uint32_t codepoint,
                       const CaseRange (&ranges)[RangeCount],
                       const SpecialCase (&special)[SpecialCount]) {
    const auto range = std::lower_bound(std::begin(ranges), std::end(ranges), codepoint,
        [](const CaseRange& entry, std::uint32_t value) { return entry.last < value; });
    if (range != std::end(ranges) && codepoint >= range->first &&
        (codepoint - range->first) % range->stride == 0) {
        const auto mapped = static_cast<std::uint32_t>(
            static_cast<int>(codepoint) + range->offset);
        output += EncodeUtf8(mapped);
        return true;
    }

    const auto entry = std::lower_bound(std::begin(special), std::end(special), codepoint,
        [](const SpecialCase& item, std::uint32_t value) { return item.codepoint < value; });
    if (entry == std::end(special) || entry->codepoint != codepoint) return false;
    output += entry->replacement;
    return true;
}

bool AppendChangedCase(std::string& output, std::uint32_t codepoint, CaseMode mode) {
    if (mode != CaseMode::Upper &&
        AppendCaseMapping(output, codepoint, kLowerRanges, kLowerSpecialCases)) {
        return true;
    }
    return mode != CaseMode::Lower &&
        AppendCaseMapping(output, codepoint, kUpperRanges, kUpperSpecialCases);
}

size_t CodepointStartAtOrBefore(const std::string& value, size_t offset) {
    if (value.empty()) {
        return 0;
    }

    offset = std::min(offset, value.size() - 1);
    while (offset > 0 &&
           (static_cast<unsigned char>(value[offset]) & 0xC0) == 0x80) {
        --offset;
    }
    return offset;
}

size_t PreviousCodepointStart(const std::string& value, size_t offset) {
    if (offset == 0 || value.empty()) {
        return 0;
    }

    --offset;
    while (offset > 0 &&
           (static_cast<unsigned char>(value[offset]) & 0xC0) == 0x80) {
        --offset;
    }
    return offset;
}

bool IsWordCharacterAt(const std::string& value, size_t offset) {
    std::uint32_t codepoint = 0;
    size_t length = 0;
    if (!DecodeUtf8At(value, offset, codepoint, length)) {
        return false;
    }
    return IsUnicodeWordCharacter(codepoint);
}

std::optional<size_t> ChangeCaseInByteRange(std::string& line, size_t start,
                                            size_t end, CaseMode mode) {
    std::string replacement;
    replacement.reserve(end - start);
    bool changed = false;
    for (size_t offset = start; offset < end;) {
        std::uint32_t codepoint = 0;
        size_t length = 0;
        const bool decoded = DecodeUtf8At(line, offset, codepoint, length);
        if (decoded && length <= end - offset &&
            AppendChangedCase(replacement, codepoint, mode)) {
            changed = true;
        } else {
            length = std::min(length, end - offset);
            replacement.append(line, offset, length);
        }
        offset += length;
    }
    if (!changed) return std::nullopt;
    line.replace(start, end - start, replacement);
    return start + replacement.size();
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

size_t LeadingIndentToErase(const std::string& line, size_t width) {
    if (!line.empty() && line.front() == '\t') return 1;
    size_t spaces = 0;
    while (spaces < width && spaces < line.size() && line[spaces] == ' ') ++spaces;
    return spaces;
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
        const size_t erase_count = LeadingIndentToErase(line, indent_width);
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

bool CanOutdentLines(const std::vector<std::string>& lines, CursorState cursor,
                     SelectionState selection) {
    if (lines.empty()) return false;
    const auto [start, end] = SelectedLineRange(lines, cursor, selection);
    return std::any_of(lines.begin() + start, lines.begin() + end + 1,
        [](const std::string& line) { return LeadingIndentToErase(line, 1) > 0; });
}

TransformResult ChangeCase(std::vector<std::string>& lines,
                           CursorState cursor,
                           SelectionState selection,
                           CaseMode mode) {
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

        const bool cursor_at_end = result.cursor.y == end.y && result.cursor.x == end.x;
        for (size_t y = start.y; y <= end.y; ++y) {
            const size_t x_start = y == start.y ? start.x : 0;
            const size_t x_end = y == end.y ? end.x : lines[y].size();
            if (const auto new_end = ChangeCaseInByteRange(lines[y], x_start, x_end, mode)) {
                result.changed = true;
                if (y == end.y) {
                    if (cursor_at_end) result.cursor.x = *new_end;
                    else result.selection.anchor_x = *new_end;
                }
            }
        }
        return result;
    }

    if (mode != CaseMode::Toggle) return result;

    std::string& line = lines[result.cursor.y];
    if (line.empty()) {
        return result;
    }

    size_t target = CodepointStartAtOrBefore(line, result.cursor.x);
    if (!IsWordCharacterAt(line, target)) {
        if (target == 0) {
            return result;
        }
        target = PreviousCodepointStart(line, target);
        if (!IsWordCharacterAt(line, target)) {
            return result;
        }
    }

    size_t start = target;
    while (start > 0) {
        const size_t previous = PreviousCodepointStart(line, start);
        if (!IsWordCharacterAt(line, previous)) {
            break;
        }
        start = previous;
    }

    size_t end = target;
    while (end < line.size()) {
        std::uint32_t codepoint = 0;
        size_t length = 0;
        if (!DecodeUtf8At(line, end, codepoint, length) ||
            !IsUnicodeWordCharacter(codepoint)) {
            break;
        }
        end += std::max<size_t>(length, 1);
    }

    result.changed = ChangeCaseInByteRange(line, start, end, mode).has_value();
    return result;
}

TransformResult ToggleCase(std::vector<std::string>& lines,
                           CursorState cursor,
                           SelectionState selection) {
    return ChangeCase(lines, cursor, selection, CaseMode::Toggle);
}

} // namespace textlt::transform
