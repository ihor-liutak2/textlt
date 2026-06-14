#include "c_family_lexer.hpp"

#include <unordered_set>

#include "lexer_common.hpp"

namespace textlt::lexers {

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

const std::unordered_set<std::string>& JsonKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "true",
        "false",
        "null",
    };
    return keywords;
}

const std::unordered_set<std::string>& GoKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "package",
        "import",
        "func",
        "return",
        "var",
        "type",
        "struct",
        "interface",
        "chan",
        "go",
        "select",
        "defer",
        "if",
        "else",
        "for",
        "range",
        "switch",
        "case",
        "default",
        "map",
        "const",
    };
    return keywords;
}

const std::unordered_set<std::string>& RustKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "fn",
        "let",
        "mut",
        "match",
        "impl",
        "trait",
        "struct",
        "enum",
        "pub",
        "use",
        "mod",
        "crate",
        "return",
        "if",
        "else",
        "loop",
        "while",
        "for",
        "in",
        "move",
        "async",
        "await",
        "type",
    };
    return keywords;
}

const std::unordered_set<std::string>& RustTypes() {
    static const std::unordered_set<std::string> types = {
        "bool",
        "char",
        "str",
        "String",
        "usize",
        "isize",
        "u8",
        "u16",
        "u32",
        "u64",
        "u128",
        "i8",
        "i16",
        "i32",
        "i64",
        "i128",
        "f32",
        "f64",
        "Self",
    };
    return types;
}

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

std::vector<SyntaxHighlighter::Token> TokenizeGo(const std::string& line) {
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

        if (line[index] == '"' || line[index] == '\'' || line[index] == '`') {
            const size_t end = line[index] == '`'
                ? ParseRawStringEnd(line, index)
                : ParseQuotedStringEnd(line, index);
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
                GoKeywords().find(word) != GoKeywords().end()
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

std::vector<SyntaxHighlighter::Token> TokenizeRust(const std::string& line) {
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
            if (line[index] == '\'' && index + 1 < line.size() &&
                IsIdentifierStart(line[index + 1])) {
                size_t lifetime_end = index + 2;
                while (lifetime_end < line.size() && IsIdentifierBody(line[lifetime_end])) {
                    ++lifetime_end;
                }
                if (lifetime_end >= line.size() || line[lifetime_end] != '\'') {
                    PushToken(tokens, index, lifetime_end, SyntaxHighlighter::Style::Type);
                    index = lifetime_end;
                    continue;
                }
            }

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

            if (index < line.size() && line[index] == '!') {
                PushToken(tokens, start, index + 1, SyntaxHighlighter::Style::Keyword);
                ++index;
                continue;
            }

            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (RustKeywords().find(word) != RustKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (RustTypes().find(word) != RustTypes().end()) {
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

std::vector<SyntaxHighlighter::Token> TokenizeJsLikeLine(const std::string& line,
                                                         bool typescript) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        index = PushJsLikeToken(tokens, line, index, line.size(), typescript);
    }

    return tokens;
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

std::vector<SyntaxHighlighter::Token> TokenizeCLikeLine(const std::string& line,
                                                        const std::string& family) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '/' && index + 1 < line.size() && line[index + 1] == '/') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (StartsWith(line, index, "/*")) {
            const size_t end = FindCommentEnd(line, index + 2, "*/");
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            index = end;
            continue;
        }

        if (line[index] == '"' ||
            (family != "json" && (line[index] == '\'' || line[index] == '`'))) {
            const size_t start = index;
            index = ParseQuotedStringEnd(line, index);
            const SyntaxHighlighter::Style style =
                family == "json" && IsJsonKey(line, index)
                    ? SyntaxHighlighter::Style::Type
                    : SyntaxHighlighter::Style::String;
            PushToken(tokens, start, index, style);
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        const bool is_identifier_start =
            (family == "javascript" || family == "typescript")
                ? IsJsIdentifierStart(line[index])
                : IsIdentifierStart(line[index]);
        if (is_identifier_start) {
            const size_t start = index;
            if (family == "cpp") {
                while (index < line.size() && IsCppWordBody(line[index])) {
                    ++index;
                }
            } else if (family == "javascript" || family == "typescript") {
                while (index < line.size() && IsJsIdentifierBody(line[index])) {
                    ++index;
                }
            } else {
                while (index < line.size() && IsIdentifierBody(line[index])) {
                    ++index;
                }
            }

            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (family == "cpp") {
                if (CppKeywords().find(word) != CppKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (CppTypes().find(word) != CppTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "java") {
                if (JavaKeywords().find(word) != JavaKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (JavaTypes().find(word) != JavaTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "javascript") {
                if (JsKeywords().find(word) != JsKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (JsTypes().find(word) != JsTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "typescript") {
                if (TypescriptKeywords().find(word) != TypescriptKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (TypescriptTypes().find(word) != TypescriptTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "json" && JsonKeywords().find(word) != JsonKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            }

            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeCpp(const std::string& line) {
    return TokenizeCLikeLine(line, "cpp");
}

std::vector<SyntaxHighlighter::Token> TokenizeJava(const std::string& line) {
    return TokenizeCLikeLine(line, "java");
}

std::vector<SyntaxHighlighter::Token> TokenizeJavascript(const std::string& line) {
    return TokenizeJsLikeLine(line, false);
}

std::vector<SyntaxHighlighter::Token> TokenizeTypescript(const std::string& line) {
    return TokenizeJsLikeLine(line, true);
}

std::vector<SyntaxHighlighter::Token> TokenizeJson(const std::string& line) {
    return TokenizeCLikeLine(line, "json");
}

} // namespace textlt::lexers
