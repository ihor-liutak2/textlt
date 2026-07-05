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
