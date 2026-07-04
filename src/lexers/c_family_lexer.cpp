#include "c_family_lexer.hpp"

#include <unordered_set>

#include "lexer_common.hpp"

namespace textlt::lexers {

const std::unordered_set<std::string>& CppKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "bitand",
        "bitor",
        "break",
        "case",
        "catch",
        "class",
        "compl",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "const_cast",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "requires",
        "reinterpret_cast",
        "return",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "using",
        "virtual",
        "volatile",
        "while",
        "xor",
        "xor_eq",
    };
    return keywords;
}

const std::unordered_set<std::string>& CppTypes() {
    static const std::unordered_set<std::string> types = {
        "auto",
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "size_t",
        "std::filesystem::path",
        "std::optional",
        "std::string",
        "std::string_view",
        "std::unordered_map",
        "std::unordered_set",
        "std::vector",
        "uint8_t",
        "uint16_t",
        "uint32_t",
        "uint64_t",
        "unsigned",
        "void",
        "wchar_t",
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

const std::unordered_set<std::string>& KotlinKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "as",
        "break",
        "class",
        "companion",
        "continue",
        "data",
        "do",
        "else",
        "for",
        "fun",
        "if",
        "import",
        "in",
        "interface",
        "is",
        "object",
        "package",
        "return",
        "sealed",
        "super",
        "this",
        "throw",
        "try",
        "typealias",
        "val",
        "var",
        "when",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& KotlinTypes() {
    static const std::unordered_set<std::string> types = {
        "Any",
        "Boolean",
        "Double",
        "Float",
        "Int",
        "Long",
        "Nothing",
        "String",
        "Unit",
        "false",
        "null",
        "true",
    };
    return types;
}

const std::unordered_set<std::string>& SwiftKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "associatedtype",
        "break",
        "case",
        "catch",
        "class",
        "continue",
        "defer",
        "do",
        "else",
        "enum",
        "extension",
        "fallthrough",
        "for",
        "func",
        "guard",
        "if",
        "import",
        "in",
        "init",
        "let",
        "protocol",
        "repeat",
        "return",
        "struct",
        "switch",
        "throw",
        "throws",
        "try",
        "var",
        "where",
        "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& SwiftTypes() {
    static const std::unordered_set<std::string> types = {
        "Any",
        "AnyObject",
        "Bool",
        "Character",
        "Double",
        "Float",
        "Int",
        "Never",
        "String",
        "UInt",
        "false",
        "nil",
        "self",
        "Self",
        "true",
    };
    return types;
}

const std::unordered_set<std::string>& DartKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "abstract",
        "async",
        "await",
        "break",
        "case",
        "class",
        "const",
        "continue",
        "else",
        "enum",
        "export",
        "extends",
        "factory",
        "final",
        "for",
        "if",
        "implements",
        "import",
        "in",
        "late",
        "mixin",
        "new",
        "part",
        "return",
        "static",
        "switch",
        "this",
        "throw",
        "try",
        "var",
        "void",
        "while",
        "with",
        "yield",
    };
    return keywords;
}

const std::unordered_set<std::string>& DartTypes() {
    static const std::unordered_set<std::string> types = {
        "bool",
        "double",
        "dynamic",
        "false",
        "int",
        "List",
        "Map",
        "null",
        "num",
        "Object",
        "Set",
        "String",
        "true",
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

bool IsPreprocessorStart(const std::string& line, size_t index) {
    if (index >= line.size() || line[index] != '#') {
        return false;
    }
    for (size_t current = 0; current < index; ++current) {
        if (!std::isspace(static_cast<unsigned char>(line[current]))) {
            return false;
        }
    }
    return true;
}

size_t ParsePreprocessorDirectiveEnd(const std::string& line, size_t index) {
    ++index;
    while (index < line.size() &&
           std::isalpha(static_cast<unsigned char>(line[index]))) {
        ++index;
    }
    return index;
}

size_t ParseIncludeTargetEnd(const std::string& line, size_t index) {
    index = SkipWhitespace(line, index);
    if (index >= line.size()) {
        return index;
    }
    if (line[index] == '"') {
        return ParseQuotedStringEnd(line, index);
    }
    if (line[index] == '<') {
        const size_t close = line.find('>', index + 1);
        return close == std::string::npos ? line.size() : close + 1;
    }
    return index;
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
        if (family == "cpp" && IsPreprocessorStart(line, index)) {
            const size_t directive_start = index;
            const size_t directive_end = ParsePreprocessorDirectiveEnd(line, index);
            PushToken(tokens, directive_start, directive_end, SyntaxHighlighter::Style::Keyword);

            const std::string directive =
                line.substr(directive_start + 1, directive_end - directive_start - 1);
            if (directive == "include" || directive == "import") {
                const size_t target_start = SkipWhitespace(line, directive_end);
                if (target_start > directive_end) {
                    PushToken(tokens, directive_end, target_start, SyntaxHighlighter::Style::Normal);
                }
                const size_t target_end = ParseIncludeTargetEnd(line, target_start);
                if (target_end > target_start) {
                    PushToken(tokens, target_start, target_end, SyntaxHighlighter::Style::String);
                    index = target_end;
                    continue;
                }
            }

            index = directive_end;
            continue;
        }

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
            } else if (family == "kotlin") {
                if (KotlinKeywords().find(word) != KotlinKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (KotlinTypes().find(word) != KotlinTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "swift") {
                if (SwiftKeywords().find(word) != SwiftKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (SwiftTypes().find(word) != SwiftTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "dart") {
                if (DartKeywords().find(word) != DartKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (DartTypes().find(word) != DartTypes().end()) {
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

std::vector<SyntaxHighlighter::Token> TokenizeKotlin(const std::string& line) {
    return TokenizeCLikeLine(line, "kotlin");
}

std::vector<SyntaxHighlighter::Token> TokenizeDart(const std::string& line) {
    return TokenizeCLikeLine(line, "dart");
}

std::vector<SyntaxHighlighter::Token> TokenizeSwift(const std::string& line) {
    return TokenizeCLikeLine(line, "swift");
}

std::vector<SyntaxHighlighter::Token> TokenizeTypescript(const std::string& line) {
    return TokenizeJsLikeLine(line, true);
}

std::vector<SyntaxHighlighter::Token> TokenizeJson(const std::string& line) {
    return TokenizeCLikeLine(line, "json");
}

} // namespace textlt::lexers
