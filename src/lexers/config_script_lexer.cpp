#include "config_script_lexer.hpp"

#include <unordered_set>

#include "lexer_common.hpp"

namespace textlt::lexers {

const std::unordered_set<std::string>& BashKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "if",
        "then",
        "else",
        "elif",
        "fi",
        "for",
        "in",
        "while",
        "until",
        "do",
        "done",
        "case",
        "esac",
        "function",
        "return",
        "exit",
        "local",
        "export",
        "alias",
        "echo",
        "printf",
    };
    return keywords;
}

const std::unordered_set<std::string>& PythonKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "def",
        "class",
        "return",
        "if",
        "elif",
        "else",
        "for",
        "while",
        "break",
        "continue",
        "import",
        "from",
        "as",
        "try",
        "except",
        "with",
        "lambda",
        "pass",
        "in",
        "is",
        "and",
        "or",
        "not",
    };
    return keywords;
}

const std::unordered_set<std::string>& PythonTypes() {
    static const std::unordered_set<std::string> types = {
        "print",
        "len",
        "range",
        "str",
        "int",
        "float",
        "list",
        "dict",
        "set",
        "tuple",
        "True",
        "False",
        "None",
        "self",
    };
    return types;
}

const std::unordered_set<std::string>& DockerfileKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "ADD",
        "ARG",
        "CMD",
        "COPY",
        "ENTRYPOINT",
        "ENV",
        "EXPOSE",
        "FROM",
        "RUN",
        "USER",
        "VOLUME",
        "WORKDIR",
    };
    return keywords;
}

const std::unordered_set<std::string>& RubyKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "class",
        "def",
        "else",
        "elsif",
        "end",
        "false",
        "if",
        "include",
        "module",
        "nil",
        "require",
        "return",
        "true",
        "unless",
        "until",
        "while",
    };
    return keywords;
}

size_t ParseBashCommandSubstitutionEnd(const std::string& line, size_t index) {
    size_t current = index + 2;
    size_t depth = 1;
    bool escaped = false;

    while (current < line.size()) {
        const char character = line[current];
        if (escaped) {
            escaped = false;
            ++current;
            continue;
        }
        if (character == '\\') {
            escaped = true;
            ++current;
            continue;
        }
        if (character == '\'' || character == '"') {
            current = ParseQuotedStringEnd(line, current);
            continue;
        }
        if (character == '`') {
            current = ParseRawStringEnd(line, current);
            continue;
        }
        if (character == '(') {
            ++depth;
        } else if (character == ')') {
            --depth;
            if (depth == 0) {
                return current + 1;
            }
        }
        ++current;
    }

    return line.size();
}

size_t ParseBashVariableEnd(const std::string& line, size_t index) {
    if (index + 1 >= line.size()) {
        return index + 1;
    }

    const char next = line[index + 1];
    if (next == '(') {
        return ParseBashCommandSubstitutionEnd(line, index);
    }
    if (next == '{') {
        const size_t close = line.find('}', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }
    if (std::isdigit(static_cast<unsigned char>(next)) ||
        next == '?' || next == '#' || next == '@' || next == '*' ||
        next == '$' || next == '!' || next == '-') {
        return index + 2;
    }
    if (!IsIdentifierStart(next)) {
        return index + 1;
    }

    size_t current = index + 2;
    while (current < line.size() && IsIdentifierBody(line[current])) {
        ++current;
    }
    return current;
}

size_t TokenizeBashDoubleQuotedString(std::vector<SyntaxHighlighter::Token>& tokens,
                                      const std::string& line,
                                      size_t index) {
    size_t current = index + 1;
    size_t string_start = index;
    bool escaped = false;

    while (current < line.size()) {
        const char character = line[current];
        if (!escaped && character == '"') {
            PushToken(tokens, string_start, current + 1, SyntaxHighlighter::Style::String);
            return current + 1;
        }
        if (!escaped && character == '$') {
            const size_t variable_end = ParseBashVariableEnd(line, current);
            if (variable_end > current + 1) {
                PushToken(tokens, string_start, current, SyntaxHighlighter::Style::String);
                PushToken(tokens, current, variable_end, SyntaxHighlighter::Style::Type);
                current = variable_end;
                string_start = current;
                escaped = false;
                continue;
            }
        }
        if (!escaped && character == '`') {
            const size_t substitution_end = ParseRawStringEnd(line, current);
            PushToken(tokens, string_start, current, SyntaxHighlighter::Style::String);
            PushToken(tokens, current, substitution_end, SyntaxHighlighter::Style::Type);
            current = substitution_end;
            string_start = current;
            escaped = false;
            continue;
        }

        escaped = character == '\\' && !escaped;
        if (character != '\\') {
            escaped = false;
        }
        ++current;
    }

    PushToken(tokens, string_start, line.size(), SyntaxHighlighter::Style::String);
    return line.size();
}

std::vector<SyntaxHighlighter::Token> TokenizeBash(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '\'') {
            const size_t end = ParseRawStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '"') {
            index = TokenizeBashDoubleQuotedString(tokens, line, index);
            continue;
        }

        if (line[index] == '`') {
            const size_t end = ParseRawStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Type);
            index = end;
            continue;
        }

        if (line[index] == '$') {
            const size_t end = ParseBashVariableEnd(line, index);
            if (end > index + 1) {
                PushToken(tokens, index, end, SyntaxHighlighter::Style::Type);
                index = end;
                continue;
            }
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            const std::string word = line.substr(start, index - start);
            const SyntaxHighlighter::Style style =
                BashKeywords().find(word) != BashKeywords().end()
                    ? SyntaxHighlighter::Style::Keyword
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeEnv(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = SkipWhitespace(line, 0);
    if (index > 0) {
        PushToken(tokens, 0, index, SyntaxHighlighter::Style::Normal);
    }

    if (index < line.size() && line[index] == '#') {
        PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
        return tokens;
    }

    const size_t equals = line.find('=', index);
    if (equals == std::string::npos) {
        PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Normal);
        return tokens;
    }

    size_t key_end = equals;
    while (key_end > index && std::isspace(static_cast<unsigned char>(line[key_end - 1]))) {
        --key_end;
    }
    PushToken(tokens, index, key_end, SyntaxHighlighter::Style::Keyword);
    PushToken(tokens, key_end, equals, SyntaxHighlighter::Style::Normal);
    PushToken(tokens, equals, equals + 1, SyntaxHighlighter::Style::Type);

    index = equals + 1;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        const size_t start = index;
        while (index < line.size() && line[index] != '#' &&
               line[index] != '"' && line[index] != '\'') {
            ++index;
        }
        PushToken(tokens, start, index, SyntaxHighlighter::Style::String);
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeIni(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = SkipWhitespace(line, 0);
    if (index > 0) {
        PushToken(tokens, 0, index, SyntaxHighlighter::Style::Normal);
    }

    if (index < line.size() && (line[index] == ';' || line[index] == '#')) {
        PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
        return tokens;
    }

    if (index < line.size() && line[index] == '[') {
        const size_t close = line.find(']', index + 1);
        const size_t end = close == std::string::npos ? line.size() : close + 1;
        PushToken(tokens, index, end, SyntaxHighlighter::Style::Keyword);
        if (end < line.size()) {
            PushToken(tokens, end, line.size(), SyntaxHighlighter::Style::Normal);
        }
        return tokens;
    }

    const size_t equals = line.find('=', index);
    if (equals == std::string::npos) {
        PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Normal);
        return tokens;
    }

    size_t key_end = equals;
    while (key_end > index && std::isspace(static_cast<unsigned char>(line[key_end - 1]))) {
        --key_end;
    }
    PushToken(tokens, index, key_end, SyntaxHighlighter::Style::Keyword);
    PushToken(tokens, key_end, equals + 1, SyntaxHighlighter::Style::Normal);

    index = equals + 1;
    while (index < line.size()) {
        if (line[index] == ';' || line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeRuby(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == ':' && index + 1 < line.size() && IsIdentifierStart(line[index + 1])) {
            const size_t start = index;
            index += 2;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Type);
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            const std::string word = line.substr(start, index - start);
            const SyntaxHighlighter::Style style =
                RubyKeywords().find(word) != RubyKeywords().end()
                    ? SyntaxHighlighter::Style::Keyword
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

bool IsYamlKeyBody(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '_' || character == '-';
}

std::vector<SyntaxHighlighter::Token> TokenizeYaml(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsIdentifierStart(line[index]) || line[index] == '-') {
            const size_t start = index;
            while (index < line.size() && IsYamlKeyBody(line[index])) {
                ++index;
            }
            const size_t lookahead = SkipWhitespace(line, index);
            const SyntaxHighlighter::Style style =
                lookahead < line.size() && line[lookahead] == ':'
                    ? SyntaxHighlighter::Style::Keyword
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeDockerfile(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            const std::string word = line.substr(start, index - start);
            const SyntaxHighlighter::Style style =
                DockerfileKeywords().find(word) != DockerfileKeywords().end()
                    ? SyntaxHighlighter::Style::Keyword
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizePython(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (StartsWith(line, index, "\"\"\"") || StartsWith(line, index, "'''")) {
            const std::string close_marker = line.substr(index, 3);
            const size_t close = line.find(close_marker, index + 3);
            const size_t end = close == std::string::npos ? line.size() : close + 3;
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }

            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (PythonKeywords().find(word) != PythonKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (PythonTypes().find(word) != PythonTypes().end()) {
                style = SyntaxHighlighter::Style::Type;
            }
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

} // namespace textlt::lexers
