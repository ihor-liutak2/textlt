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
