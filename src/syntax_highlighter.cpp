#include "syntax_highlighter.hpp"

#include <cctype>
#include <unordered_set>

namespace textlt {
namespace {

enum State {
    STATE_NORMAL,
    STATE_KEYWORD,
    STATE_STRING,
    STATE_COMMENT,
    STATE_NUMBER,
};

enum class Language {
    Cpp,
    Json,
    Plain,
};

Language LanguageFromExtension(const std::string& extension) {
    if (extension == ".json") {
        return Language::Json;
    }
    if (extension == ".c" || extension == ".cc" || extension == ".cpp" ||
        extension == ".cxx" || extension == ".h" || extension == ".hh" ||
        extension == ".hpp" || extension == ".hxx") {
        return Language::Cpp;
    }
    return Language::Plain;
}

bool IsIdentifierStart(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalpha(value) || character == '_';
}

bool IsIdentifierBody(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '_';
}

bool IsCppWordBody(char character) {
    return IsIdentifierBody(character) || character == ':';
}

bool IsBoundaryBefore(const std::string& line, size_t index) {
    return index == 0 || !IsIdentifierBody(line[index - 1]);
}

bool IsNumberStart(const std::string& line, size_t index) {
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

size_t ParseNumberEnd(const std::string& line, size_t index) {
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

const std::unordered_set<std::string>& CppKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "if",
        "else",
        "return",
        "while",
        "for",
        "class",
        "struct",
        "switch",
        "case",
        "public",
        "private",
        "protected",
    };
    return keywords;
}

const std::unordered_set<std::string>& CppTypes() {
    static const std::unordered_set<std::string> types = {
        "void",
        "int",
        "float",
        "double",
        "bool",
        "char",
        "auto",
        "size_t",
        "std::string",
        "std::vector",
    };
    return types;
}

const std::unordered_set<std::string>& JsonKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "true",
        "false",
        "null",
    };
    return keywords;
}

bool IsJsonKey(const std::string& line, size_t after_string) {
    size_t current = after_string;
    while (current < line.size() &&
           std::isspace(static_cast<unsigned char>(line[current]))) {
        ++current;
    }
    return current < line.size() && line[current] == ':';
}

void PushToken(std::vector<SyntaxHighlighter::Token>& tokens,
               size_t start,
               size_t end,
               SyntaxHighlighter::Style style) {
    if (end <= start) {
        return;
    }
    if (!tokens.empty() &&
        tokens.back().start + tokens.back().length == start &&
        tokens.back().style == style) {
        tokens.back().length += end - start;
        return;
    }
    tokens.push_back({start, end - start, style});
}

} // namespace

ftxui::Color SyntaxHighlighter::ColorForStyle(Style style, const Theme& theme) {
    switch (style) {
        case Style::Keyword: return theme.syntax_keyword;
        case Style::Type: return theme.syntax_type;
        case Style::String: return theme.syntax_string;
        case Style::Comment: return theme.syntax_comment;
        case Style::Number: return theme.syntax_number;
        case Style::Normal: return theme.editor_text;
    }
    return theme.editor_text;
}

std::vector<SyntaxHighlighter::Token> SyntaxHighlighter::TokenizeLine(
    const std::string& line,
    const std::string& file_extension) {
    const Language language = LanguageFromExtension(file_extension);
    std::vector<Token> tokens;
    tokens.reserve(line.size() / 4 + 1);
    if (language == Language::Plain) {
        PushToken(tokens, 0, line.size(), Style::Normal);
        return tokens;
    }

    State state = STATE_NORMAL;
    size_t index = 0;
    while (index < line.size()) {
        state = STATE_NORMAL;

        if (line[index] == '/' && index + 1 < line.size() && line[index + 1] == '/') {
            state = STATE_COMMENT;
            PushToken(tokens, index, line.size(), Style::Comment);
            break;
        }

        if (line[index] == '"') {
            state = STATE_STRING;
            const size_t start = index;
            ++index;
            bool escaped = false;
            while (index < line.size()) {
                const char current = line[index];
                if (current == '"' && !escaped) {
                    ++index;
                    break;
                }
                escaped = current == '\\' && !escaped;
                if (current != '\\') {
                    escaped = false;
                }
                ++index;
            }

            const Style style =
                language == Language::Json && IsJsonKey(line, index)
                    ? Style::Type
                    : Style::String;
            PushToken(tokens, start, index, style);
            continue;
        }

        if (IsNumberStart(line, index)) {
            state = STATE_NUMBER;
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            state = STATE_KEYWORD;
            const size_t start = index;
            if (language == Language::Cpp) {
                while (index < line.size() && IsCppWordBody(line[index])) {
                    ++index;
                }
            } else {
                while (index < line.size() && IsIdentifierBody(line[index])) {
                    ++index;
                }
            }

            const std::string word = line.substr(start, index - start);
            Style style = Style::Normal;
            if (language == Language::Cpp) {
                if (CppKeywords().find(word) != CppKeywords().end()) {
                    style = Style::Keyword;
                } else if (CppTypes().find(word) != CppTypes().end()) {
                    style = Style::Type;
                }
            } else if (language == Language::Json &&
                       JsonKeywords().find(word) != JsonKeywords().end()) {
                style = Style::Keyword;
            }

            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, Style::Normal);
        ++index;
    }

    return tokens;
}

ftxui::Element SyntaxHighlighter::HighlightLine(const std::string& line,
                                                const Theme& theme,
                                                const std::string& file_extension) {
    ftxui::Elements elements;
    for (const Token& token : TokenizeLine(line, file_extension)) {
        elements.push_back(
            ftxui::text(line.substr(token.start, token.length)) |
            ftxui::color(ColorForStyle(token.style, theme)));
    }
    return ftxui::hbox(std::move(elements));
}

} // namespace textlt
