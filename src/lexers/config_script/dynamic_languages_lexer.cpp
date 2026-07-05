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
