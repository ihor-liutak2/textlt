#include "config_script_lexer.hpp"

#include <algorithm>
#include <cctype>
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

const std::unordered_set<std::string>& CMakeKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "add_compile_definitions",
        "add_compile_options",
        "add_custom_command",
        "add_custom_target",
        "add_definitions",
        "add_executable",
        "add_library",
        "add_subdirectory",
        "cmake_minimum_required",
        "configure_file",
        "else",
        "elseif",
        "endforeach",
        "endfunction",
        "endif",
        "endmacro",
        "endwhile",
        "find_package",
        "foreach",
        "function",
        "if",
        "include",
        "include_directories",
        "install",
        "link_directories",
        "list",
        "macro",
        "message",
        "option",
        "project",
        "set",
        "set_property",
        "target_compile_definitions",
        "target_compile_features",
        "target_compile_options",
        "target_include_directories",
        "target_link_libraries",
        "target_sources",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& CMakeConstants() {
    static const std::unordered_set<std::string> constants = {
        "AND",
        "BOOL",
        "CACHE",
        "FALSE",
        "INTERFACE",
        "LANGUAGES",
        "OFF",
        "ON",
        "OR",
        "PRIVATE",
        "PUBLIC",
        "REQUIRED",
        "STATIC",
        "TRUE",
        "VERSION",
    };
    return constants;
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

const std::unordered_set<std::string>& PowershellKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "begin",
        "catch",
        "class",
        "continue",
        "data",
        "do",
        "else",
        "elseif",
        "end",
        "enum",
        "exit",
        "filter",
        "finally",
        "for",
        "foreach",
        "from",
        "function",
        "if",
        "in",
        "param",
        "process",
        "return",
        "switch",
        "throw",
        "trap",
        "try",
        "until",
        "using",
        "while",
        "workflow",
    };
    return keywords;
}

const std::unordered_set<std::string>& PowershellTypes() {
    static const std::unordered_set<std::string> types = {
        "bool",
        "byte",
        "datetime",
        "decimal",
        "double",
        "false",
        "hashtable",
        "int",
        "long",
        "null",
        "object",
        "pscustomobject",
        "string",
        "true",
    };
    return types;
}

const std::unordered_set<std::string>& HclKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "check",
        "data",
        "for_each",
        "import",
        "locals",
        "module",
        "moved",
        "output",
        "provider",
        "provisioner",
        "removed",
        "resource",
        "terraform",
        "variable",
    };
    return keywords;
}

const std::unordered_set<std::string>& HclConstants() {
    static const std::unordered_set<std::string> constants = {
        "false",
        "null",
        "true",
    };
    return constants;
}

const std::unordered_set<std::string>& TomlConstants() {
    static const std::unordered_set<std::string> constants = {
        "false",
        "inf",
        "nan",
        "true",
    };
    return constants;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

size_t ParsePowershellVariableEnd(const std::string& line, size_t index) {
    if (index + 1 >= line.size()) {
        return index + 1;
    }

    const char next = line[index + 1];
    if (next == '{') {
        const size_t close = line.find('}', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }

    size_t current = index + 1;
    while (current < line.size()) {
        const char character = line[current];
        const unsigned char value = static_cast<unsigned char>(character);
        if (!std::isalnum(value) && character != '_' && character != ':' && character != '?') {
            break;
        }
        ++current;
    }
    return current == index + 1 ? index + 1 : current;
}

bool IsTomlBareKeyBody(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '_' || character == '-';
}

size_t FindTomlCommentStart(const std::string& line, size_t start) {
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    for (size_t index = start; index < line.size(); ++index) {
        const char character = line[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_double) {
            if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_double = false;
            }
            continue;
        }
        if (in_single) {
            if (character == '\'') {
                in_single = false;
            }
            continue;
        }
        if (character == '"') {
            in_double = true;
            continue;
        }
        if (character == '\'') {
            in_single = true;
            continue;
        }
        if (character == '#') {
            return index;
        }
    }
    return std::string::npos;
}

const std::unordered_set<std::string>& LuaKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "and",
        "break",
        "do",
        "else",
        "elseif",
        "end",
        "for",
        "function",
        "goto",
        "if",
        "in",
        "local",
        "not",
        "or",
        "repeat",
        "return",
        "then",
        "until",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& LuaTypes() {
    static const std::unordered_set<std::string> types = {
        "_ENV",
        "_G",
        "arg",
        "assert",
        "coroutine",
        "debug",
        "false",
        "io",
        "ipairs",
        "math",
        "nil",
        "os",
        "pairs",
        "pcall",
        "print",
        "require",
        "self",
        "string",
        "table",
        "tonumber",
        "tostring",
        "true",
        "type",
        "utf8",
    };
    return types;
}

size_t ParseLuaLongBracketClose(const std::string& line, size_t index) {
    if (index >= line.size() || line[index] != '[') {
        return index;
    }

    size_t equals = index + 1;
    while (equals < line.size() && line[equals] == '=') {
        ++equals;
    }
    if (equals >= line.size() || line[equals] != '[') {
        return index;
    }

    const std::string close_marker = "]" + std::string(equals - index - 1, '=') + "]";
    const size_t close = line.find(close_marker, equals + 1);
    return close == std::string::npos ? line.size() : close + close_marker.size();
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

std::vector<SyntaxHighlighter::Token> TokenizeLua(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (StartsWith(line, index, "--")) {
            const size_t long_comment_end = ParseLuaLongBracketClose(line, index + 2);
            const size_t end = long_comment_end > index + 2
                ? long_comment_end
                : line.size();
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            index = end;
            continue;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '[') {
            const size_t end = ParseLuaLongBracketClose(line, index);
            if (end > index) {
                PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
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
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (LuaKeywords().find(word) != LuaKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (LuaTypes().find(word) != LuaTypes().end()) {
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

std::vector<SyntaxHighlighter::Token> TokenizeCMake(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '$' && index + 1 < line.size() && line[index + 1] == '{') {
            const size_t close = line.find('}', index + 2);
            const size_t end = close == std::string::npos ? line.size() : close + 1;
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Type);
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
            while (index < line.size() &&
                   (IsIdentifierBody(line[index]) || line[index] == '-')) {
                ++index;
            }
            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;

            size_t next = SkipWhitespace(line, index);
            if (next < line.size() && line[next] == '(' &&
                CMakeKeywords().find(word) != CMakeKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (CMakeConstants().find(ToUpper(word)) != CMakeConstants().end()) {
                style = SyntaxHighlighter::Style::Type;
            } else if (!word.empty() &&
                       std::all_of(word.begin(), word.end(), [](unsigned char character) {
                           return std::isupper(character) || std::isdigit(character) || character == '_';
                       })) {
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


size_t ParseMakeVariableEnd(const std::string& line, size_t index) {
    if (index + 1 >= line.size()) {
        return index + 1;
    }
    const char next = line[index + 1];
    if (next == '(') {
        const size_t close = line.find(')', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }
    if (next == '{') {
        const size_t close = line.find('}', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }
    if (IsIdentifierStart(next)) {
        size_t current = index + 2;
        while (current < line.size() && IsIdentifierBody(line[current])) {
            ++current;
        }
        return current;
    }
    return index + 2;
}

std::vector<SyntaxHighlighter::Token> TokenizeMakeValue(const std::string& line,
                                                        size_t start,
                                                        size_t end) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve((end - start) / 4 + 1);

    size_t index = start;
    while (index < end) {
        if (line[index] == '#') {
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            break;
        }
        if (line[index] == '$') {
            const size_t variable_end = std::min(ParseMakeVariableEnd(line, index), end);
            PushToken(tokens, index, variable_end, SyntaxHighlighter::Style::Type);
            index = variable_end;
            continue;
        }
        if (line[index] == '"' || line[index] == '\'') {
            const size_t string_end = std::min(ParseQuotedStringEnd(line, index), end);
            PushToken(tokens, index, string_end, SyntaxHighlighter::Style::String);
            index = string_end;
            continue;
        }
        if (IsNumberStart(line, index)) {
            const size_t number_end = std::min(ParseNumberEnd(line, index), end);
            PushToken(tokens, index, number_end, SyntaxHighlighter::Style::Number);
            index = number_end;
            continue;
        }
        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

void PushMakeShiftedTokens(std::vector<SyntaxHighlighter::Token>& tokens,
                           const std::vector<SyntaxHighlighter::Token>& source,
                           size_t offset) {
    for (const SyntaxHighlighter::Token& token : source) {
        PushToken(tokens, offset + token.start, offset + token.start + token.length, token.style);
    }
}

std::vector<SyntaxHighlighter::Token> TokenizeMakefile(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = SkipWhitespace(line, 0);
    if (index > 0) {
        PushToken(tokens, 0, index, SyntaxHighlighter::Style::Normal);
    }

    if (index >= line.size()) {
        return tokens;
    }

    if (line[index] == '#') {
        PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
        return tokens;
    }

    if (line[0] == '\t') {
        PushMakeShiftedTokens(tokens, TokenizeBash(line.substr(1)), 1);
        return tokens;
    }

    const size_t assign = line.find('=', index);
    const size_t colon = line.find(':', index);
    const bool make_assignment_operator = assign != std::string::npos && assign > index &&
        (line[assign - 1] == ':' || line[assign - 1] == '?' || line[assign - 1] == '+');
    const bool variable_assignment = assign != std::string::npos &&
        (colon == std::string::npos || assign < colon || make_assignment_operator);

    if (variable_assignment) {
        size_t operator_start = assign;
        if (operator_start > index &&
            (line[operator_start - 1] == ':' || line[operator_start - 1] == '?' ||
             line[operator_start - 1] == '+')) {
            --operator_start;
        }
        size_t key_end = operator_start;
        while (key_end > index && std::isspace(static_cast<unsigned char>(line[key_end - 1]))) {
            --key_end;
        }
        PushToken(tokens, index, key_end, SyntaxHighlighter::Style::Type);
        PushToken(tokens, key_end, assign + 1, SyntaxHighlighter::Style::Normal);
        PushMakeShiftedTokens(tokens, TokenizeMakeValue(line, assign + 1, line.size()), 0);
        return tokens;
    }

    if (colon != std::string::npos) {
        PushToken(tokens, index, colon, SyntaxHighlighter::Style::Keyword);
        PushToken(tokens, colon, colon + 1, SyntaxHighlighter::Style::Normal);
        PushMakeShiftedTokens(tokens, TokenizeMakeValue(line, colon + 1, line.size()), 0);
        return tokens;
    }

    PushMakeShiftedTokens(tokens, TokenizeMakeValue(line, index, line.size()), 0);
    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeMarkdown(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    const size_t first_text = line.find_first_not_of(" \t");
    if (first_text == std::string::npos) {
        PushToken(tokens, 0, line.size(), SyntaxHighlighter::Style::Normal);
        return tokens;
    }

    if (line.compare(first_text, 3, "```") == 0 ||
        line.compare(first_text, 3, "~~~") == 0) {
        PushToken(tokens, 0, first_text, SyntaxHighlighter::Style::Normal);
        PushToken(tokens, first_text, line.size(), SyntaxHighlighter::Style::Comment);
        return tokens;
    }

    if (line[first_text] == '#') {
        size_t marker_end = first_text;
        while (marker_end < line.size() && line[marker_end] == '#') {
            ++marker_end;
        }
        if (marker_end < line.size() && std::isspace(static_cast<unsigned char>(line[marker_end]))) {
            PushToken(tokens, 0, first_text, SyntaxHighlighter::Style::Normal);
            PushToken(tokens, first_text, marker_end, SyntaxHighlighter::Style::Keyword);
            PushToken(tokens, marker_end, line.size(), SyntaxHighlighter::Style::Type);
            return tokens;
        }
    }

    if (line.compare(first_text, 2, "> ") == 0 || line[first_text] == '>') {
        PushToken(tokens, 0, first_text, SyntaxHighlighter::Style::Normal);
        PushToken(tokens, first_text, line.size(), SyntaxHighlighter::Style::Comment);
        return tokens;
    }

    if ((line[first_text] == '-' || line[first_text] == '*' || line[first_text] == '+') &&
        first_text + 1 < line.size() &&
        std::isspace(static_cast<unsigned char>(line[first_text + 1]))) {
        PushToken(tokens, 0, first_text, SyntaxHighlighter::Style::Normal);
        PushToken(tokens, first_text, first_text + 1, SyntaxHighlighter::Style::Keyword);
        PushToken(tokens, first_text + 1, line.size(), SyntaxHighlighter::Style::Normal);
        return tokens;
    }

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '`') {
            const size_t end = ParseRawStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '[') {
            const size_t label_end = line.find(']', index + 1);
            if (label_end != std::string::npos &&
                label_end + 1 < line.size() && line[label_end + 1] == '(') {
                const size_t url_end = line.find(')', label_end + 2);
                if (url_end != std::string::npos) {
                    PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Keyword);
                    PushToken(tokens, index + 1, label_end, SyntaxHighlighter::Style::Type);
                    PushToken(tokens, label_end, label_end + 2, SyntaxHighlighter::Style::Keyword);
                    PushToken(tokens, label_end + 2, url_end, SyntaxHighlighter::Style::String);
                    PushToken(tokens, url_end, url_end + 1, SyntaxHighlighter::Style::Keyword);
                    index = url_end + 1;
                    continue;
                }
            }
        }

        if (line.compare(index, 2, "**") == 0 || line.compare(index, 2, "__") == 0) {
            PushToken(tokens, index, index + 2, SyntaxHighlighter::Style::Keyword);
            index += 2;
            continue;
        }

        if (line[index] == '*' || line[index] == '_') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Keyword);
            ++index;
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

std::vector<SyntaxHighlighter::Token> TokenizePowershell(const std::string& line) {
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

        if (line[index] == '$') {
            const size_t end = ParsePowershellVariableEnd(line, index);
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

        if (line[index] == '[') {
            size_t end = index + 1;
            while (end < line.size() && line[end] != ']') {
                ++end;
            }
            if (end < line.size() && end > index + 1) {
                PushToken(tokens, index, end + 1, SyntaxHighlighter::Style::Type);
                index = end + 1;
                continue;
            }
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < line.size() &&
                   (IsIdentifierBody(line[index]) || line[index] == '-')) {
                ++index;
            }
            const std::string word = ToLower(line.substr(start, index - start));
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (PowershellKeywords().find(word) != PowershellKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (PowershellTypes().find(word) != PowershellTypes().end()) {
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

std::vector<SyntaxHighlighter::Token> TokenizeToml(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = SkipWhitespace(line, 0);
    if (index > 0) {
        PushToken(tokens, 0, index, SyntaxHighlighter::Style::Normal);
    }

    if (index >= line.size()) {
        return tokens;
    }

    const size_t comment = FindTomlCommentStart(line, index);
    const size_t content_end = comment == std::string::npos ? line.size() : comment;

    if (line[index] == '[') {
        const size_t close = line.find(']', index + 1);
        const size_t end = close == std::string::npos || close >= content_end ? content_end : close + 1;
        PushToken(tokens, index, end, SyntaxHighlighter::Style::Keyword);
        if (comment != std::string::npos) {
            PushToken(tokens, comment, line.size(), SyntaxHighlighter::Style::Comment);
        }
        return tokens;
    }

    size_t equals = std::string::npos;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    for (size_t current = index; current < content_end; ++current) {
        const char character = line[current];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_double) {
            if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_double = false;
            }
            continue;
        }
        if (in_single) {
            if (character == '\'') {
                in_single = false;
            }
            continue;
        }
        if (character == '"') {
            in_double = true;
            continue;
        }
        if (character == '\'') {
            in_single = true;
            continue;
        }
        if (character == '=') {
            equals = current;
            break;
        }
    }

    if (equals != std::string::npos) {
        size_t key_end = equals;
        while (key_end > index && std::isspace(static_cast<unsigned char>(line[key_end - 1]))) {
            --key_end;
        }
        PushToken(tokens, index, key_end, SyntaxHighlighter::Style::Type);
        PushToken(tokens, key_end, equals + 1, SyntaxHighlighter::Style::Normal);
        index = equals + 1;
    }

    while (index < content_end) {
        index = SkipWhitespace(line, index);
        if (index >= content_end) {
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = std::min(ParseQuotedStringEnd(line, index), content_end);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = std::min(ParseNumberEnd(line, index), content_end);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < content_end && IsTomlBareKeyBody(line[index])) {
                ++index;
            }
            const std::string word = ToLower(line.substr(start, index - start));
            const SyntaxHighlighter::Style style =
                TomlConstants().find(word) != TomlConstants().end()
                    ? SyntaxHighlighter::Style::Keyword
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    if (comment != std::string::npos) {
        PushToken(tokens, comment, line.size(), SyntaxHighlighter::Style::Comment);
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeHcl(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (StartsWith(line, index, "//")) {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (StartsWith(line, index, "/*")) {
            const size_t end = FindCommentEnd(line, index + 2, "*/");
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
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
            const std::string lowered = ToLower(word);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            size_t lookahead = SkipWhitespace(line, index);
            if (HclKeywords().find(lowered) != HclKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (HclConstants().find(lowered) != HclConstants().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (lookahead < line.size() && line[lookahead] == '=') {
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
