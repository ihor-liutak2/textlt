const std::unordered_set<std::string>& RKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "break", "else", "for", "function", "if", "in", "next", "repeat", "return", "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& RTypes() {
    static const std::unordered_set<std::string> types = {
        "FALSE", "Inf", "NA", "NaN", "NULL", "TRUE", "data.frame", "factor", "list", "matrix",
    };
    return types;
}

const std::unordered_set<std::string>& JuliaKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "abstract", "baremodule", "begin", "break", "catch", "const", "continue", "do", "else",
        "elseif", "end", "export", "finally", "for", "function", "if", "import", "in", "let",
        "macro", "module", "mutable", "primitive", "quote", "return", "struct", "try", "using", "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& JuliaTypes() {
    static const std::unordered_set<std::string> types = {
        "Any", "Bool", "Dict", "Float32", "Float64", "Int", "Int32", "Int64", "Nothing",
        "String", "Tuple", "Vector", "false", "missing", "nothing", "true",
    };
    return types;
}

const std::unordered_set<std::string>& ZigKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "align", "allowzero", "and", "anyframe", "asm", "async", "await", "break", "catch", "comptime",
        "const", "continue", "defer", "else", "enum", "errdefer", "error", "export", "extern", "fn",
        "for", "if", "inline", "noalias", "nosuspend", "opaque", "or", "orelse", "packed", "pub",
        "resume", "return", "struct", "suspend", "switch", "test", "threadlocal", "try", "union", "unreachable",
        "usingnamespace", "var", "volatile", "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& ZigTypes() {
    static const std::unordered_set<std::string> types = {
        "anyerror", "bool", "comptime_float", "comptime_int", "false", "f16", "f32", "f64", "f80", "f128",
        "i8", "i16", "i32", "i64", "i128", "isize", "null", "true", "type", "u8", "u16", "u32",
        "u64", "u128", "undefined", "usize", "void",
    };
    return types;
}

const std::unordered_set<std::string>& PerlKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "bless", "chomp", "die", "do", "else", "elsif", "foreach", "for", "if", "last", "local",
        "my", "next", "our", "package", "print", "redo", "require", "return", "sub", "unless", "until", "use", "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& PerlTypes() {
    static const std::unordered_set<std::string> types = {
        "undef", "STDERR", "STDIN", "STDOUT",
    };
    return types;
}

const std::unordered_set<std::string>& ScalaKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "abstract", "case", "catch", "class", "def", "do", "else", "enum", "export", "extends",
        "false", "final", "finally", "for", "given", "if", "implicit", "import", "lazy", "match",
        "new", "null", "object", "override", "package", "private", "protected", "return", "sealed", "super",
        "then", "this", "throw", "trait", "true", "try", "type", "val", "var", "while", "with", "yield",
    };
    return keywords;
}

const std::unordered_set<std::string>& ScalaTypes() {
    static const std::unordered_set<std::string> types = {
        "Any", "AnyRef", "Boolean", "Double", "Float", "Int", "List", "Long", "Map", "Nil", "Option", "Seq", "String", "Unit",
    };
    return types;
}

const std::unordered_set<std::string>& ElixirKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "after", "alias", "and", "case", "catch", "cond", "def", "defdelegate", "defguard", "defimpl",
        "defmacro", "defmodule", "defp", "defprotocol", "do", "else", "end", "fn", "for", "if", "import",
        "in", "not", "or", "quote", "raise", "receive", "require", "rescue", "try", "unless", "unquote", "use", "when", "with",
    };
    return keywords;
}

const std::unordered_set<std::string>& ElixirTypes() {
    static const std::unordered_set<std::string> types = {
        "false", "nil", "true",
    };
    return types;
}

const std::unordered_set<std::string>& ErlangKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "after", "and", "andalso", "begin", "case", "catch", "cond", "end", "fun", "if", "let",
        "not", "of", "or", "orelse", "query", "receive", "try", "when", "xor",
    };
    return keywords;
}

const std::unordered_set<std::string>& ErlangTypes() {
    static const std::unordered_set<std::string> types = {
        "false", "nil", "true",
    };
    return types;
}

const std::unordered_set<std::string>& MatlabKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "break", "case", "catch", "classdef", "continue", "else", "elseif", "end", "for", "function",
        "global", "if", "methods", "otherwise", "parfor", "persistent", "properties", "return", "switch", "try", "while",
    };
    return keywords;
}

const std::unordered_set<std::string>& MatlabTypes() {
    static const std::unordered_set<std::string> types = {
        "false", "inf", "NaN", "nan", "nargin", "nargout", "pi", "true",
    };
    return types;
}

const std::unordered_set<std::string>& AssemblyKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "adc", "add", "and", "b", "beq", "bne", "call", "cmp", "db", "dd", "dq", "dw", "global",
        "int", "ja", "jb", "je", "jg", "jl", "jmp", "jne", "lea", "mov", "mul", "nop", "or", "pop",
        "push", "ret", "section", "shl", "shr", "sub", "syscall", "test", "xor",
    };
    return keywords;
}

const std::unordered_set<std::string>& AssemblyRegisters() {
    static const std::unordered_set<std::string> registers = {
        "al", "ah", "ax", "eax", "rax", "bl", "bh", "bx", "ebx", "rbx", "cl", "ch", "cx", "ecx", "rcx",
        "dl", "dh", "dx", "edx", "rdx", "esi", "rsi", "edi", "rdi", "esp", "rsp", "ebp", "rbp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "xmm0", "xmm1", "xmm2", "xmm3",
    };
    return registers;
}

bool IsExtraBody(char character, const std::string& extra_body) {
    return extra_body.find(character) != std::string::npos;
}

size_t ParseWordEnd(const std::string& line, size_t index, const std::string& extra_body = "") {
    while (index < line.size() &&
           (IsIdentifierBody(line[index]) || IsExtraBody(line[index], extra_body))) {
        ++index;
    }
    return index;
}

size_t ParseSigilVariableEnd(const std::string& line, size_t index) {
    if (index + 1 >= line.size()) {
        return index + 1;
    }
    if (line[index + 1] == '{') {
        const size_t close = line.find('}', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }
    size_t current = index + 1;
    if (current < line.size() && IsIdentifierStart(line[current])) {
        return ParseWordEnd(line, current);
    }
    return index + 1;
}

std::vector<SyntaxHighlighter::Token> TokenizeKeywordScript(
    const std::string& line,
    const std::unordered_set<std::string>& keywords,
    const std::unordered_set<std::string>& types,
    const std::vector<std::string>& line_comments,
    const std::string& extra_identifier_body = "",
    const std::string& variable_prefixes = "",
    bool uppercase_identifiers_are_types = false,
    bool lowercase_words = false) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        bool handled_comment = false;
        for (const std::string& marker : line_comments) {
            if (StartsWith(line, index, marker)) {
                PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
                return tokens;
            }
        }
        if (handled_comment) {
            continue;
        }

        if (StartsWith(line, index, "/*")) {
            const size_t end = FindCommentEnd(line, index + 2, "*/");
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            index = end;
            continue;
        }

        if (StartsWith(line, index, "#=")) {
            const size_t end = FindCommentEnd(line, index + 2, "=#");
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

        if (variable_prefixes.find(line[index]) != std::string::npos) {
            const size_t end = ParseSigilVariableEnd(line, index);
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

        const bool starts_with_dot = line[index] == '.' &&
            index + 1 < line.size() && IsIdentifierStart(line[index + 1]);
        if (IsIdentifierStart(line[index]) || starts_with_dot) {
            const size_t start = index;
            if (starts_with_dot) {
                ++index;
            }
            index = ParseWordEnd(line, index, extra_identifier_body);
            const std::string word = line.substr(start, index - start);
            const std::string lookup = lowercase_words ? ToLower(word) : word;
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (keywords.find(lookup) != keywords.end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (types.find(lookup) != types.end()) {
                style = SyntaxHighlighter::Style::Type;
            } else if (uppercase_identifiers_are_types && !word.empty() &&
                       std::isupper(static_cast<unsigned char>(word.front()))) {
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

std::vector<SyntaxHighlighter::Token> TokenizeR(const std::string& line) {
    return TokenizeKeywordScript(line, RKeywords(), RTypes(), {"#"}, ".");
}

std::vector<SyntaxHighlighter::Token> TokenizeJulia(const std::string& line) {
    return TokenizeKeywordScript(line, JuliaKeywords(), JuliaTypes(), {"#"});
}

std::vector<SyntaxHighlighter::Token> TokenizeZig(const std::string& line) {
    return TokenizeKeywordScript(line, ZigKeywords(), ZigTypes(), {"//"});
}

std::vector<SyntaxHighlighter::Token> TokenizePerl(const std::string& line) {
    return TokenizeKeywordScript(line, PerlKeywords(), PerlTypes(), {"#"}, "", "$@%");
}

std::vector<SyntaxHighlighter::Token> TokenizeScala(const std::string& line) {
    return TokenizeKeywordScript(line, ScalaKeywords(), ScalaTypes(), {"//"});
}

std::vector<SyntaxHighlighter::Token> TokenizeElixir(const std::string& line) {
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
            const size_t end = ParseWordEnd(line, index + 1);
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
            index = ParseWordEnd(line, index, "!");
            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (ElixirKeywords().find(word) != ElixirKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (ElixirTypes().find(word) != ElixirTypes().end() ||
                       std::isupper(static_cast<unsigned char>(word.front()))) {
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

std::vector<SyntaxHighlighter::Token> TokenizeErlang(const std::string& line) {
    return TokenizeKeywordScript(line, ErlangKeywords(), ErlangTypes(), {"%"}, "_", "", true);
}

std::vector<SyntaxHighlighter::Token> TokenizeMatlab(const std::string& line) {
    return TokenizeKeywordScript(line, MatlabKeywords(), MatlabTypes(), {"%"}, "", "", false, false);
}

std::vector<SyntaxHighlighter::Token> TokenizeAssembly(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == ';' || line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }
        if (StartsWith(line, index, "//")) {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }
        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }
        if (line[index] == '%') {
            const size_t start = index;
            ++index;
            index = ParseWordEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Type);
            continue;
        }
        if (line[index] == '.' && index + 1 < line.size() && IsIdentifierStart(line[index + 1])) {
            const size_t start = index;
            ++index;
            index = ParseWordEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Keyword);
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
            index = ParseWordEnd(line, index);
            const std::string word = ToLower(line.substr(start, index - start));
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (AssemblyKeywords().find(word) != AssemblyKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (AssemblyRegisters().find(word) != AssemblyRegisters().end() ||
                       (index < line.size() && line[index] == ':')) {
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
