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


std::vector<SyntaxHighlighter::Token> TokenizeStylesheetVariant(const std::string& line,
                                                                char variable_prefix) {
    const size_t first_text = SkipWhitespace(line, 0);
    if (first_text < line.size() && StartsWith(line, first_text, "//")) {
        std::vector<SyntaxHighlighter::Token> tokens;
        tokens.reserve(2);
        if (first_text > 0) {
            PushToken(tokens, 0, first_text, SyntaxHighlighter::Style::Normal);
        }
        PushToken(tokens, first_text, line.size(), SyntaxHighlighter::Style::Comment);
        return tokens;
    }

    if (first_text < line.size() && line[first_text] == variable_prefix) {
        const size_t colon = line.find(':', first_text + 1);
        if (colon != std::string::npos) {
            std::vector<SyntaxHighlighter::Token> tokens;
            tokens.reserve(line.size() / 4 + 1);
            if (first_text > 0) {
                PushToken(tokens, 0, first_text, SyntaxHighlighter::Style::Normal);
            }
            size_t variable_end = colon;
            while (variable_end > first_text &&
                   std::isspace(static_cast<unsigned char>(line[variable_end - 1]))) {
                --variable_end;
            }
            PushToken(tokens, first_text, variable_end, SyntaxHighlighter::Style::Type);
            PushToken(tokens, variable_end, colon + 1, SyntaxHighlighter::Style::Normal);
            size_t value_end = colon + 1;
            while (value_end < line.size() && line[value_end] != ';') {
                if (StartsWith(line, value_end, "/*") || StartsWith(line, value_end, "//")) {
                    break;
                }
                ++value_end;
            }
            PushCssValueTokens(tokens, line, colon + 1, value_end);
            if (value_end < line.size() && line[value_end] == ';') {
                PushToken(tokens, value_end, value_end + 1, SyntaxHighlighter::Style::Normal);
                ++value_end;
            }
            if (value_end < line.size()) {
                PushToken(tokens, value_end, line.size(), SyntaxHighlighter::Style::Comment);
            }
            return tokens;
        }
    }

    return TokenizeCss(line);
}

std::vector<SyntaxHighlighter::Token> TokenizeScss(const std::string& line) {
    return TokenizeStylesheetVariant(line, '$');
}

std::vector<SyntaxHighlighter::Token> TokenizeSass(const std::string& line) {
    return TokenizeStylesheetVariant(line, '$');
}

std::vector<SyntaxHighlighter::Token> TokenizeLess(const std::string& line) {
    return TokenizeStylesheetVariant(line, '@');
}
