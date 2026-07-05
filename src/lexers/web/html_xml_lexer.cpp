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

std::vector<SyntaxHighlighter::Token> TokenizeXmlTagContent(
    const std::string& line,
    size_t index,
    size_t end) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve((end - index) / 4 + 1);

    size_t current = index;
    if (current < end && line[current] == '/') {
        PushToken(tokens, current, current + 1, SyntaxHighlighter::Style::Normal);
        ++current;
    }

    const size_t name_start = SkipWhitespace(line, current);
    if (name_start > current) {
        PushToken(tokens, current, name_start, SyntaxHighlighter::Style::Normal);
        current = name_start;
    }
    if (current < end && IsHtmlNameStart(line[current])) {
        const size_t start = current;
        while (current < end && IsHtmlNameBody(line[current])) {
            ++current;
        }
        PushToken(tokens, start, current, SyntaxHighlighter::Style::Keyword);
    }

    while (current < end) {
        if (line[current] == '"' || line[current] == '\'') {
            const size_t string_end = std::min(ParseQuotedStringEnd(line, current), end);
            PushToken(tokens, current, string_end, SyntaxHighlighter::Style::String);
            current = string_end;
            continue;
        }

        if (IsHtmlNameStart(line[current])) {
            const size_t start = current;
            while (current < end && IsHtmlNameBody(line[current])) {
                ++current;
            }

            const size_t lookahead = SkipWhitespace(line, current);
            const SyntaxHighlighter::Style style =
                lookahead < end && line[lookahead] == '='
                    ? SyntaxHighlighter::Style::Type
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, current, style);
            continue;
        }

        PushToken(tokens, current, current + 1, SyntaxHighlighter::Style::Normal);
        ++current;
    }

    return tokens;
}

size_t FindXmlTagEnd(const std::string& line, size_t index, const std::string& marker) {
    char quote = '\0';
    while (index + marker.size() <= line.size()) {
        if (quote != '\0') {
            if (line[index] == quote) {
                quote = '\0';
            }
            ++index;
            continue;
        }

        if (line[index] == '"' || line[index] == '\'') {
            quote = line[index];
            ++index;
            continue;
        }

        if (StartsWith(line, index, marker)) {
            return index;
        }

        ++index;
    }

    return std::string::npos;
}

std::vector<SyntaxHighlighter::Token> TokenizeXml(const std::string& line) {
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

        if (StartsWith(line, index, "<![CDATA[")) {
            const size_t close = line.find("]]>", index + 9);
            const size_t end = close == std::string::npos ? line.size() : close + 3;
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
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

        if (StartsWith(line, index, "<?")) {
            const size_t close = FindXmlTagEnd(line, index + 2, "?>");
            const size_t body_end = close == std::string::npos ? line.size() : close;
            PushToken(tokens, index, index + 2, SyntaxHighlighter::Style::Normal);
            PushShiftedTokens(
                tokens,
                TokenizeXmlTagContent(line, index + 2, body_end),
                0);
            if (close != std::string::npos) {
                PushToken(tokens, close, close + 2, SyntaxHighlighter::Style::Normal);
                index = close + 2;
            } else {
                index = body_end;
            }
            continue;
        }

        if (StartsWith(line, index, "<!")) {
            const size_t close = FindXmlTagEnd(line, index + 2, ">");
            const size_t end = close == std::string::npos ? line.size() : close + 1;
            PushToken(tokens, index, index + 2, SyntaxHighlighter::Style::Normal);
            PushShiftedTokens(
                tokens,
                TokenizeXmlTagContent(line, index + 2, end - (close == std::string::npos ? 0 : 1)),
                0);
            if (close != std::string::npos) {
                PushToken(tokens, close, close + 1, SyntaxHighlighter::Style::Normal);
            }
            index = end;
            continue;
        }

        const size_t close = FindXmlTagEnd(line, index + 1, ">");
        const size_t body_end = close == std::string::npos ? line.size() : close;
        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        PushShiftedTokens(tokens, TokenizeXmlTagContent(line, index + 1, body_end), 0);
        if (close != std::string::npos) {
            PushToken(tokens, close, close + 1, SyntaxHighlighter::Style::Normal);
            index = close + 1;
        } else {
            index = body_end;
        }
    }

    return tokens;
}
