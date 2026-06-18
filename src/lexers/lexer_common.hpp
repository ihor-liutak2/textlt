#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "syntax_highlighter.hpp"

namespace textlt::lexers {

inline bool IsIdentifierStart(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalpha(value) || character == '_';
}

inline bool IsIdentifierBody(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '_';
}

inline bool IsJsIdentifierStart(char character) {
    return IsIdentifierStart(character) || character == '$';
}

inline bool IsJsIdentifierBody(char character) {
    return IsIdentifierBody(character) || character == '$';
}

inline bool IsCppWordBody(char character) {
    return IsIdentifierBody(character) || character == ':';
}

inline bool IsBoundaryBefore(const std::string& line, size_t index) {
    return index == 0 || !IsIdentifierBody(line[index - 1]);
}

inline bool IsNumberStart(const std::string& line, size_t index) {
    if (!IsBoundaryBefore(line, index)) {
        return false;
    }

    const unsigned char current = static_cast<unsigned char>(line[index]);
    if (std::isdigit(current)) {
        return true;
    }
    if ((line[index] == '-' || line[index] == '+') && index + 1 < line.size()) {
        const char next = line[index + 1];
        return std::isdigit(static_cast<unsigned char>(next)) ||
            (next == '.' && index + 2 < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[index + 2])));
    }
    return line[index] == '.' && index + 1 < line.size() &&
        std::isdigit(static_cast<unsigned char>(line[index + 1]));
}

inline size_t ParseNumberEnd(const std::string& line, size_t index) {
    const size_t size = line.size();
    size_t current = index;

    if (current < size && (line[current] == '-' || line[current] == '+')) {
        ++current;
    }

    if (current < size && line[current] == '.') {
        ++current;
        while (current < size && std::isdigit(static_cast<unsigned char>(line[current]))) {
            ++current;
        }
    } else if (current + 1 < size && line[current] == '0' &&
               (line[current + 1] == 'x' || line[current + 1] == 'X')) {
        current += 2;
        while (current < size && std::isxdigit(static_cast<unsigned char>(line[current]))) {
            ++current;
        }
    } else {
        while (current < size && std::isdigit(static_cast<unsigned char>(line[current]))) {
            ++current;
        }
        if (current < size && line[current] == '.') {
            ++current;
            while (current < size && std::isdigit(static_cast<unsigned char>(line[current]))) {
                ++current;
            }
        }
    }

    if (current < size && (line[current] == 'e' || line[current] == 'E')) {
        const size_t exponent = current;
        ++current;
        if (current < size && (line[current] == '+' || line[current] == '-')) {
            ++current;
        }
        const size_t exponent_digits = current;
        while (current < size && std::isdigit(static_cast<unsigned char>(line[current]))) {
            ++current;
        }
        if (current == exponent_digits) {
            current = exponent;
        }
    }

    while (current < size &&
           (std::isalpha(static_cast<unsigned char>(line[current])) || line[current] == '_')) {
        ++current;
    }

    return current;
}

inline bool StartsWith(const std::string& line, size_t index, const std::string& marker) {
    return index + marker.size() <= line.size() &&
        line.compare(index, marker.size(), marker) == 0;
}

inline size_t FindCommentEnd(const std::string& line,
                             size_t index,
                             const std::string& close_marker) {
    const size_t close = line.find(close_marker, index);
    return close == std::string::npos ? line.size() : close + close_marker.size();
}

inline size_t ParseQuotedStringEnd(const std::string& line, size_t index) {
    const char quote = line[index];
    ++index;
    bool escaped = false;
    while (index < line.size()) {
        const char current = line[index];
        if (current == quote && !escaped) {
            ++index;
            break;
        }
        escaped = current == '\\' && !escaped;
        if (current != '\\') {
            escaped = false;
        }
        ++index;
    }
    return index;
}

inline size_t ParseRawStringEnd(const std::string& line, size_t index) {
    const char quote = line[index];
    const size_t close = line.find(quote, index + 1);
    return close == std::string::npos ? line.size() : close + 1;
}

inline size_t SkipWhitespace(const std::string& line, size_t index) {
    while (index < line.size() &&
           std::isspace(static_cast<unsigned char>(line[index]))) {
        ++index;
    }
    return index;
}

inline size_t TrimRight(const std::string& line, size_t end) {
    while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1]))) {
        --end;
    }
    return end;
}

inline bool IsLineExactlyIdentifier(const std::string& line, const std::string& identifier) {
    const size_t start = SkipWhitespace(line, 0);
    size_t end = TrimRight(line, line.size());
    if (end > start && line[end - 1] == ';') {
        --end;
        end = TrimRight(line, end);
    }
    return end >= start && line.substr(start, end - start) == identifier;
}

inline std::string ToUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

inline bool IsJsonKey(const std::string& line, size_t after_string) {
    size_t current = after_string;
    while (current < line.size() &&
           std::isspace(static_cast<unsigned char>(line[current]))) {
        ++current;
    }
    return current < line.size() && line[current] == ':';
}

inline bool IsHtmlNameStart(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalpha(value) || character == '_' || character == ':' || character == '!';
}

inline bool IsHtmlNameBody(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '-' || character == '_' ||
        character == ':' || character == '!';
}

inline void PushToken(std::vector<SyntaxHighlighter::Token>& tokens,
                      size_t start,
                      size_t end,
                      SyntaxHighlighter::Style style) {
    if (end <= start) {
        return;
    }
    tokens.push_back({start, end - start, style});
}

} // namespace textlt::lexers
