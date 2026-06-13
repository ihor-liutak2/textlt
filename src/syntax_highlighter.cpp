#include "syntax_highlighter.hpp"

#include <algorithm>
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
    Csharp,
    Css,
    Dockerfile,
    Graphql,
    Ini,
    Html,
    Java,
    Javascript,
    Jsx,
    Json,
    Php,
    Plain,
    Python,
    Ruby,
    Sql,
    Typescript,
    Tsx,
    Yaml,
};

std::string BaseName(const std::string& path) {
    const size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

std::string ExtensionFromName(const std::string& filename) {
    const size_t dot = filename.find_last_of('.');
    return dot == std::string::npos ? "" : filename.substr(dot);
}

Language LanguageFromPath(const std::string& path) {
    const std::string filename = BaseName(path);
    const std::string extension = ExtensionFromName(filename);

    if (filename.rfind("Dockerfile", 0) == 0) {
        return Language::Dockerfile;
    }
    if (filename == "docker-compose.yml" || filename == "docker-compose.yaml") {
        return Language::Yaml;
    }
    if (filename == "Gemfile") {
        return Language::Ruby;
    }
    if (extension == ".conf" || extension == ".ini") {
        return Language::Ini;
    }
    if (extension == ".cs") {
        return Language::Csharp;
    }
    if (extension == ".css") {
        return Language::Css;
    }
    if (extension == ".gql" || extension == ".graphql") {
        return Language::Graphql;
    }
    if (extension == ".html" || extension == ".htm") {
        return Language::Html;
    }
    if (extension == ".js" || extension == ".mjs" || extension == ".cjs") {
        return Language::Javascript;
    }
    if (extension == ".jsx") {
        return Language::Jsx;
    }
    if (extension == ".java") {
        return Language::Java;
    }
    if (extension == ".json") {
        return Language::Json;
    }
    if (extension == ".php") {
        return Language::Php;
    }
    if (extension == ".py") {
        return Language::Python;
    }
    if (extension == ".rb") {
        return Language::Ruby;
    }
    if (extension == ".sql") {
        return Language::Sql;
    }
    if (extension == ".ts" || extension == ".mts") {
        return Language::Typescript;
    }
    if (extension == ".tsx") {
        return Language::Tsx;
    }
    if (extension == ".yaml" || extension == ".yml") {
        return Language::Yaml;
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

bool IsJsIdentifierStart(char character) {
    return IsIdentifierStart(character) || character == '$';
}

bool IsJsIdentifierBody(char character) {
    return IsIdentifierBody(character) || character == '$';
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

bool StartsWith(const std::string& line, size_t index, const std::string& marker) {
    return index + marker.size() <= line.size() &&
        line.compare(index, marker.size(), marker) == 0;
}

size_t FindCommentEnd(const std::string& line,
                      size_t index,
                      const std::string& close_marker) {
    const size_t close = line.find(close_marker, index);
    return close == std::string::npos ? line.size() : close + close_marker.size();
}

size_t ParseQuotedStringEnd(const std::string& line, size_t index) {
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

size_t SkipWhitespace(const std::string& line, size_t index) {
    while (index < line.size() &&
           std::isspace(static_cast<unsigned char>(line[index]))) {
        ++index;
    }
    return index;
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

const std::unordered_set<std::string>& JsKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "if",
        "else",
        "return",
        "for",
        "while",
        "switch",
        "case",
        "break",
        "function",
        "class",
        "export",
        "import",
    };
    return keywords;
}

const std::unordered_set<std::string>& JsTypes() {
    static const std::unordered_set<std::string> types = {
        "const",
        "let",
        "var",
        "true",
        "false",
        "null",
        "undefined",
        "document",
        "window",
    };
    return types;
}

const std::unordered_set<std::string>& TypescriptKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "const",
        "let",
        "var",
        "function",
        "return",
        "if",
        "else",
        "for",
        "while",
        "switch",
        "case",
        "break",
        "class",
        "export",
        "import",
        "from",
        "as",
        "extends",
        "implements",
        "new",
        "this",
        "yield",
        "await",
        "async",
    };
    return keywords;
}

const std::unordered_set<std::string>& TypescriptTypes() {
    static const std::unordered_set<std::string> types = {
        "interface",
        "type",
        "enum",
        "public",
        "private",
        "protected",
        "readonly",
        "abstract",
        "keyof",
        "any",
        "unknown",
        "never",
        "void",
        "string",
        "number",
        "boolean",
        "undefined",
        "null",
    };
    return types;
}

const std::unordered_set<std::string>& JavaKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "public",
        "private",
        "protected",
        "class",
        "interface",
        "enum",
        "extends",
        "implements",
        "return",
        "if",
        "else",
        "for",
        "while",
        "switch",
        "case",
        "new",
        "this",
        "super",
        "final",
        "static",
        "void",
        "abstract",
        "synchronized",
        "throw",
        "throws",
        "try",
        "catch",
    };
    return keywords;
}

const std::unordered_set<std::string>& JavaTypes() {
    static const std::unordered_set<std::string> types = {
        "int",
        "float",
        "double",
        "boolean",
        "char",
        "byte",
        "long",
        "short",
        "String",
        "Object",
        "System",
        "Integer",
        "List",
        "ArrayList",
        "Map",
        "HashMap",
    };
    return types;
}

const std::unordered_set<std::string>& PhpKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "function",
        "class",
        "public",
        "private",
        "protected",
        "return",
        "if",
        "else",
        "elseif",
        "foreach",
        "as",
        "while",
        "switch",
        "case",
        "echo",
        "include",
        "require",
        "namespace",
        "use",
        "array",
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

const std::unordered_set<std::string>& JsonKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "true",
        "false",
        "null",
    };
    return keywords;
}

const std::unordered_set<std::string>& CsharpKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "async",
        "await",
        "class",
        "else",
        "foreach",
        "get",
        "if",
        "internal",
        "namespace",
        "new",
        "private",
        "protected",
        "public",
        "return",
        "set",
        "string",
        "struct",
        "using",
        "var",
        "void",
        "int",
    };
    return keywords;
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

const std::unordered_set<std::string>& GraphqlKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "enum",
        "fragment",
        "input",
        "interface",
        "mutation",
        "query",
        "scalar",
        "schema",
        "subscription",
        "type",
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

const std::unordered_set<std::string>& SqlKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "BY",
        "CREATE",
        "DELETE",
        "DROP",
        "FROM",
        "GROUP",
        "INDEX",
        "INNER",
        "INSERT",
        "INTO",
        "JOIN",
        "LEFT",
        "ON",
        "ORDER",
        "RIGHT",
        "SELECT",
        "TABLE",
        "UPDATE",
        "VALUES",
        "WHERE",
    };
    return keywords;
}

std::string ToUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
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
               SyntaxHighlighter::Style style);

std::vector<SyntaxHighlighter::Token> TokenizeCsharp(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
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
            const SyntaxHighlighter::Style style =
                CsharpKeywords().find(word) != CsharpKeywords().end()
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

std::vector<SyntaxHighlighter::Token> TokenizeGraphql(const std::string& line) {
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

        if (line[index] == '$' && index + 1 < line.size() && IsIdentifierStart(line[index + 1])) {
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
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (GraphqlKeywords().find(word) != GraphqlKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else {
                const size_t lookahead = SkipWhitespace(line, index);
                if (lookahead < line.size() && line[lookahead] == ':') {
                    style = SyntaxHighlighter::Style::Type;
                }
            }
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
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

std::vector<SyntaxHighlighter::Token> TokenizeSql(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (StartsWith(line, index, "--")) {
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
            const std::string word = ToUpper(line.substr(start, index - start));
            const SyntaxHighlighter::Style style =
                SqlKeywords().find(word) != SqlKeywords().end()
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

bool IsHtmlNameStart(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalpha(value) || character == '_' || character == ':' || character == '!';
}

bool IsHtmlNameBody(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '-' || character == '_' ||
        character == ':' || character == '!';
}

std::vector<SyntaxHighlighter::Token> TokenizeHtml(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (StartsWith(line, index, "<!--")) {
            const size_t end = FindCommentEnd(line, index + 4, "-->");
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            index = end;
            continue;
        }

        if (line[index] != '<') {
            const size_t next_tag = line.find('<', index);
            const size_t end = next_tag == std::string::npos ? line.size() : next_tag;
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Normal);
            index = end;
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;

        if (index < line.size() && line[index] == '/') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
        }

        const size_t tag_name_start = SkipWhitespace(line, index);
        if (tag_name_start > index) {
            PushToken(tokens, index, tag_name_start, SyntaxHighlighter::Style::Normal);
            index = tag_name_start;
        }

        if (index < line.size() && IsHtmlNameStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsHtmlNameBody(line[index])) {
                ++index;
            }
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Keyword);
        }

        while (index < line.size() && line[index] != '>') {
            if (line[index] == '"' || line[index] == '\'') {
                const size_t end = ParseQuotedStringEnd(line, index);
                PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
                index = end;
                continue;
            }

            if (IsHtmlNameStart(line[index])) {
                const size_t start = index;
                while (index < line.size() && IsHtmlNameBody(line[index])) {
                    ++index;
                }

                const size_t lookahead = SkipWhitespace(line, index);
                const SyntaxHighlighter::Style style =
                    lookahead < line.size() && line[lookahead] == '='
                        ? SyntaxHighlighter::Style::Type
                        : SyntaxHighlighter::Style::Normal;
                PushToken(tokens, start, index, style);
                continue;
            }

            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
        }

        if (index < line.size() && line[index] == '>') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
        }
    }

    return tokens;
}

bool HasCssPropertyAhead(const std::string& line, size_t index) {
    const size_t colon = line.find(':', index);
    if (colon == std::string::npos) {
        return false;
    }

    const size_t open_brace = line.find('{', index);
    const size_t close_brace = line.find('}', index);
    return open_brace == std::string::npos &&
        (close_brace == std::string::npos || colon < close_brace);
}

void PushCssValueTokens(std::vector<SyntaxHighlighter::Token>& tokens,
                        const std::string& line,
                        size_t start,
                        size_t end) {
    size_t index = start;
    while (index < end) {
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
        if (std::isspace(static_cast<unsigned char>(line[index]))) {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        } else {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::String);
        }
        ++index;
    }
}

std::vector<SyntaxHighlighter::Token> TokenizeCss(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    bool inside_block = false;
    size_t index = 0;
    while (index < line.size()) {
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

        if (line[index] == '{') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
            inside_block = true;
            continue;
        }

        if (line[index] == '}') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
            inside_block = false;
            continue;
        }

        if (!inside_block && !HasCssPropertyAhead(line, index)) {
            const size_t brace = line.find('{', index);
            const size_t comment = line.find("/*", index);
            size_t end = brace == std::string::npos ? line.size() : brace;
            if (comment != std::string::npos && comment < end) {
                end = comment;
            }
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Type);
            index = end;
            continue;
        }

        const size_t colon = line.find(':', index);
        if (colon == std::string::npos) {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Normal);
            break;
        }

        const size_t property_start = SkipWhitespace(line, index);
        if (property_start > index) {
            PushToken(tokens, index, property_start, SyntaxHighlighter::Style::Normal);
        }

        size_t property_end = colon;
        while (property_end > property_start &&
               std::isspace(static_cast<unsigned char>(line[property_end - 1]))) {
            --property_end;
        }
        PushToken(tokens, property_start, property_end, SyntaxHighlighter::Style::Keyword);
        PushToken(tokens, property_end, colon + 1, SyntaxHighlighter::Style::Normal);

        size_t value_end = colon + 1;
        while (value_end < line.size() && line[value_end] != ';' && line[value_end] != '}') {
            if (StartsWith(line, value_end, "/*")) {
                break;
            }
            ++value_end;
        }
        PushCssValueTokens(tokens, line, colon + 1, value_end);
        index = value_end;

        if (index < line.size() && line[index] == ';') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
        }
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

SyntaxHighlighter::Style JsLikeWordStyle(const std::string& word, bool typescript) {
    if (typescript) {
        if (TypescriptKeywords().find(word) != TypescriptKeywords().end()) {
            return SyntaxHighlighter::Style::Keyword;
        }
        if (TypescriptTypes().find(word) != TypescriptTypes().end()) {
            return SyntaxHighlighter::Style::Type;
        }
        return SyntaxHighlighter::Style::Normal;
    }

    if (JsKeywords().find(word) != JsKeywords().end()) {
        return SyntaxHighlighter::Style::Keyword;
    }
    if (JsTypes().find(word) != JsTypes().end()) {
        return SyntaxHighlighter::Style::Type;
    }
    return SyntaxHighlighter::Style::Normal;
}

bool IsJsxTagStart(const std::string& line, size_t index) {
    if (index + 1 >= line.size() || line[index] != '<') {
        return false;
    }

    if (std::isalpha(static_cast<unsigned char>(line[index + 1]))) {
        return true;
    }

    return index + 2 < line.size() && line[index + 1] == '/' &&
        std::isalpha(static_cast<unsigned char>(line[index + 2]));
}

size_t PushJsLikeToken(std::vector<SyntaxHighlighter::Token>& tokens,
                       const std::string& line,
                       size_t index,
                       size_t end,
                       bool typescript) {
    if (index >= end) {
        return index;
    }

    if (index + 1 < end && line[index] == '/' && line[index + 1] == '/') {
        PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
        return end;
    }

    if (index + 1 < end && StartsWith(line, index, "/*")) {
        const size_t comment_end = std::min(FindCommentEnd(line, index + 2, "*/"), end);
        PushToken(tokens, index, comment_end, SyntaxHighlighter::Style::Comment);
        return comment_end;
    }

    if (line[index] == '"' || line[index] == '\'' || line[index] == '`') {
        const size_t string_end = std::min(ParseQuotedStringEnd(line, index), end);
        PushToken(tokens, index, string_end, SyntaxHighlighter::Style::String);
        return string_end;
    }

    if (IsNumberStart(line, index)) {
        const size_t number_end = std::min(ParseNumberEnd(line, index), end);
        PushToken(tokens, index, number_end, SyntaxHighlighter::Style::Number);
        return number_end;
    }

    if (IsJsIdentifierStart(line[index])) {
        const size_t start = index;
        while (index < end && IsJsIdentifierBody(line[index])) {
            ++index;
        }
        const std::string word = line.substr(start, index - start);
        PushToken(tokens, start, index, JsLikeWordStyle(word, typescript));
        return index;
    }

    PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
    return index + 1;
}

size_t TokenizeJsxExpression(std::vector<SyntaxHighlighter::Token>& tokens,
                             const std::string& line,
                             size_t index,
                             bool typescript);

size_t TokenizeJsxTag(std::vector<SyntaxHighlighter::Token>& tokens,
                      const std::string& line,
                      size_t index,
                      bool typescript) {
    PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
    ++index;

    if (index < line.size() && line[index] == '/') {
        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    const size_t tag_name_start = SkipWhitespace(line, index);
    if (tag_name_start > index) {
        PushToken(tokens, index, tag_name_start, SyntaxHighlighter::Style::Normal);
        index = tag_name_start;
    }

    if (index < line.size() && IsHtmlNameStart(line[index])) {
        const size_t start = index;
        while (index < line.size() && IsHtmlNameBody(line[index])) {
            ++index;
        }
        PushToken(tokens, start, index, SyntaxHighlighter::Style::Keyword);
    }

    while (index < line.size()) {
        if (line[index] == '>') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            return index + 1;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '{') {
            index = TokenizeJsxExpression(tokens, line, index, typescript);
            continue;
        }

        if (IsHtmlNameStart(line[index])) {
            const size_t start = index;
            while (index < line.size() && IsHtmlNameBody(line[index])) {
                ++index;
            }

            const size_t lookahead = SkipWhitespace(line, index);
            const SyntaxHighlighter::Style style =
                lookahead < line.size() && line[lookahead] == '='
                    ? SyntaxHighlighter::Style::Type
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return index;
}

size_t TokenizeJsxExpression(std::vector<SyntaxHighlighter::Token>& tokens,
                             const std::string& line,
                             size_t index,
                             bool typescript) {
    size_t depth = 0;
    while (index < line.size()) {
        if (line[index] == '{') {
            ++depth;
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
            continue;
        }

        if (line[index] == '}') {
            PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
            ++index;
            if (depth <= 1) {
                return index;
            }
            --depth;
            continue;
        }

        if (IsJsxTagStart(line, index)) {
            index = TokenizeJsxTag(tokens, line, index, typescript);
            continue;
        }

        index = PushJsLikeToken(tokens, line, index, line.size(), typescript);
    }

    return index;
}

std::vector<SyntaxHighlighter::Token> TokenizeJsx(const std::string& line, bool typescript) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (IsJsxTagStart(line, index)) {
            index = TokenizeJsxTag(tokens, line, index, typescript);
            continue;
        }

        index = PushJsLikeToken(tokens, line, index, line.size(), typescript);
    }

    return tokens;
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
    const std::string& file_path) {
    const Language language = LanguageFromPath(file_path);
    std::vector<Token> tokens;
    tokens.reserve(line.size() / 4 + 1);
    if (language == Language::Csharp) {
        return TokenizeCsharp(line);
    }
    if (language == Language::Dockerfile) {
        return TokenizeDockerfile(line);
    }
    if (language == Language::Graphql) {
        return TokenizeGraphql(line);
    }
    if (language == Language::Html) {
        return TokenizeHtml(line);
    }
    if (language == Language::Ini) {
        return TokenizeIni(line);
    }
    if (language == Language::Css) {
        return TokenizeCss(line);
    }
    if (language == Language::Python) {
        return TokenizePython(line);
    }
    if (language == Language::Ruby) {
        return TokenizeRuby(line);
    }
    if (language == Language::Sql) {
        return TokenizeSql(line);
    }
    if (language == Language::Yaml) {
        return TokenizeYaml(line);
    }
    if (language == Language::Jsx) {
        return TokenizeJsx(line, false);
    }
    if (language == Language::Tsx) {
        return TokenizeJsx(line, true);
    }
    if (language == Language::Plain) {
        PushToken(tokens, 0, line.size(), Style::Normal);
        return tokens;
    }

    State state = STATE_NORMAL;
    size_t index = 0;
    while (index < line.size()) {
        state = STATE_NORMAL;

        if (language == Language::Php && StartsWith(line, index, "<?php")) {
            PushToken(tokens, index, index + 5, Style::Keyword);
            index += 5;
            continue;
        }

        if (language == Language::Php && StartsWith(line, index, "?>")) {
            PushToken(tokens, index, index + 2, Style::Keyword);
            index += 2;
            continue;
        }

        if ((language == Language::Cpp || language == Language::Javascript ||
             language == Language::Java || language == Language::Php ||
             language == Language::Typescript) &&
            line[index] == '/' && index + 1 < line.size() && line[index + 1] == '/') {
            state = STATE_COMMENT;
            PushToken(tokens, index, line.size(), Style::Comment);
            break;
        }

        if (language == Language::Php && line[index] == '#') {
            state = STATE_COMMENT;
            PushToken(tokens, index, line.size(), Style::Comment);
            break;
        }

        if ((language == Language::Cpp || language == Language::Javascript ||
             language == Language::Java || language == Language::Php ||
             language == Language::Typescript) &&
            StartsWith(line, index, "/*")) {
            state = STATE_COMMENT;
            const size_t end = FindCommentEnd(line, index + 2, "*/");
            PushToken(tokens, index, end, Style::Comment);
            index = end;
            continue;
        }

        if (line[index] == '"' ||
            ((language == Language::Cpp || language == Language::Javascript ||
              language == Language::Java || language == Language::Php ||
              language == Language::Typescript) &&
             (line[index] == '\'' || line[index] == '`'))) {
            state = STATE_STRING;
            const size_t start = index;
            index = ParseQuotedStringEnd(line, index);

            const Style style =
                language == Language::Json && IsJsonKey(line, index)
                    ? Style::Type
                    : Style::String;
            PushToken(tokens, start, index, style);
            continue;
        }

        if (language == Language::Php && line[index] == '$' &&
            index + 1 < line.size() && IsIdentifierStart(line[index + 1])) {
            const size_t start = index;
            index += 2;
            while (index < line.size() && IsIdentifierBody(line[index])) {
                ++index;
            }
            PushToken(tokens, start, index, Style::Type);
            continue;
        }

        if (IsNumberStart(line, index)) {
            state = STATE_NUMBER;
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, Style::Number);
            continue;
        }

        const bool is_identifier_start =
            (language == Language::Javascript || language == Language::Typescript)
            ? IsJsIdentifierStart(line[index])
            : IsIdentifierStart(line[index]);
        if (is_identifier_start) {
            state = STATE_KEYWORD;
            const size_t start = index;
            if (language == Language::Cpp) {
                while (index < line.size() && IsCppWordBody(line[index])) {
                    ++index;
                }
            } else if (language == Language::Javascript ||
                       language == Language::Typescript) {
                while (index < line.size() && IsJsIdentifierBody(line[index])) {
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
            } else if (language == Language::Java) {
                if (JavaKeywords().find(word) != JavaKeywords().end()) {
                    style = Style::Keyword;
                } else if (JavaTypes().find(word) != JavaTypes().end()) {
                    style = Style::Type;
                }
            } else if (language == Language::Javascript) {
                if (JsKeywords().find(word) != JsKeywords().end()) {
                    style = Style::Keyword;
                } else if (JsTypes().find(word) != JsTypes().end()) {
                    style = Style::Type;
                }
            } else if (language == Language::Typescript) {
                if (TypescriptKeywords().find(word) != TypescriptKeywords().end()) {
                    style = Style::Keyword;
                } else if (TypescriptTypes().find(word) != TypescriptTypes().end()) {
                    style = Style::Type;
                }
            } else if (language == Language::Php) {
                if (PhpKeywords().find(word) != PhpKeywords().end()) {
                    style = Style::Keyword;
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
                                                const std::string& file_path) {
    ftxui::Elements elements;
    for (const Token& token : TokenizeLine(line, file_path)) {
        elements.push_back(
            ftxui::text(line.substr(token.start, token.length)) |
            ftxui::color(ColorForStyle(token.style, theme)));
    }
    return ftxui::hbox(std::move(elements));
}

} // namespace textlt
