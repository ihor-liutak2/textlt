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
