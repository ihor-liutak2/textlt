std::vector<SyntaxHighlighter::Token> TokenizeEnv(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = SkipWhitespace(line, 0);
    if (index > 0) {
        PushToken(tokens, 0, index, SyntaxHighlighter::Style::Normal);
    }

    if (index < line.size() && line[index] == '#') {
        PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
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
    PushToken(tokens, key_end, equals, SyntaxHighlighter::Style::Normal);
    PushToken(tokens, equals, equals + 1, SyntaxHighlighter::Style::Type);

    index = equals + 1;
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

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        const size_t start = index;
        while (index < line.size() && line[index] != '#' &&
               line[index] != '"' && line[index] != '\'') {
            ++index;
        }
        PushToken(tokens, start, index, SyntaxHighlighter::Style::String);
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

std::vector<SyntaxHighlighter::Token> TokenizeCMake(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (line[index] == '"') {
            const size_t end = ParseQuotedStringEnd(line, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (line[index] == '$' && index + 1 < line.size() && line[index + 1] == '{') {
            const size_t close = line.find('}', index + 2);
            const size_t end = close == std::string::npos ? line.size() : close + 1;
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
            while (index < line.size() &&
                   (IsIdentifierBody(line[index]) || line[index] == '-')) {
                ++index;
            }
            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;

            size_t next = SkipWhitespace(line, index);
            if (next < line.size() && line[next] == '(' &&
                CMakeKeywords().find(word) != CMakeKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (CMakeConstants().find(ToUpper(word)) != CMakeConstants().end()) {
                style = SyntaxHighlighter::Style::Type;
            } else if (!word.empty() &&
                       std::all_of(word.begin(), word.end(), [](unsigned char character) {
                           return std::isupper(character) || std::isdigit(character) || character == '_';
                       })) {
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


size_t ParseMakeVariableEnd(const std::string& line, size_t index) {
    if (index + 1 >= line.size()) {
        return index + 1;
    }
    const char next = line[index + 1];
    if (next == '(') {
        const size_t close = line.find(')', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }
    if (next == '{') {
        const size_t close = line.find('}', index + 2);
        return close == std::string::npos ? line.size() : close + 1;
    }
    if (IsIdentifierStart(next)) {
        size_t current = index + 2;
        while (current < line.size() && IsIdentifierBody(line[current])) {
            ++current;
        }
        return current;
    }
    return index + 2;
}

std::vector<SyntaxHighlighter::Token> TokenizeMakeValue(const std::string& line,
                                                        size_t start,
                                                        size_t end) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve((end - start) / 4 + 1);

    size_t index = start;
    while (index < end) {
        if (line[index] == '#') {
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            break;
        }
        if (line[index] == '$') {
            const size_t variable_end = std::min(ParseMakeVariableEnd(line, index), end);
            PushToken(tokens, index, variable_end, SyntaxHighlighter::Style::Type);
            index = variable_end;
            continue;
        }
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
        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

void PushMakeShiftedTokens(std::vector<SyntaxHighlighter::Token>& tokens,
                           const std::vector<SyntaxHighlighter::Token>& source,
                           size_t offset) {
    for (const SyntaxHighlighter::Token& token : source) {
        PushToken(tokens, offset + token.start, offset + token.start + token.length, token.style);
    }
}

std::vector<SyntaxHighlighter::Token> TokenizeMakefile(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = SkipWhitespace(line, 0);
    if (index > 0) {
        PushToken(tokens, 0, index, SyntaxHighlighter::Style::Normal);
    }

    if (index >= line.size()) {
        return tokens;
    }

    if (line[index] == '#') {
        PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
        return tokens;
    }

    if (line[0] == '\t') {
        PushMakeShiftedTokens(tokens, TokenizeBash(line.substr(1)), 1);
        return tokens;
    }

    const size_t assign = line.find('=', index);
    const size_t colon = line.find(':', index);
    const bool make_assignment_operator = assign != std::string::npos && assign > index &&
        (line[assign - 1] == ':' || line[assign - 1] == '?' || line[assign - 1] == '+');
    const bool variable_assignment = assign != std::string::npos &&
        (colon == std::string::npos || assign < colon || make_assignment_operator);

    if (variable_assignment) {
        size_t operator_start = assign;
        if (operator_start > index &&
            (line[operator_start - 1] == ':' || line[operator_start - 1] == '?' ||
             line[operator_start - 1] == '+')) {
            --operator_start;
        }
        size_t key_end = operator_start;
        while (key_end > index && std::isspace(static_cast<unsigned char>(line[key_end - 1]))) {
            --key_end;
        }
        PushToken(tokens, index, key_end, SyntaxHighlighter::Style::Type);
        PushToken(tokens, key_end, assign + 1, SyntaxHighlighter::Style::Normal);
        PushMakeShiftedTokens(tokens, TokenizeMakeValue(line, assign + 1, line.size()), 0);
        return tokens;
    }

    if (colon != std::string::npos) {
        PushToken(tokens, index, colon, SyntaxHighlighter::Style::Keyword);
        PushToken(tokens, colon, colon + 1, SyntaxHighlighter::Style::Normal);
        PushMakeShiftedTokens(tokens, TokenizeMakeValue(line, colon + 1, line.size()), 0);
        return tokens;
    }

    PushMakeShiftedTokens(tokens, TokenizeMakeValue(line, index, line.size()), 0);
    return tokens;
}


std::vector<SyntaxHighlighter::Token> TokenizeToml(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = SkipWhitespace(line, 0);
    if (index > 0) {
        PushToken(tokens, 0, index, SyntaxHighlighter::Style::Normal);
    }

    if (index >= line.size()) {
        return tokens;
    }

    const size_t comment = FindTomlCommentStart(line, index);
    const size_t content_end = comment == std::string::npos ? line.size() : comment;

    if (line[index] == '[') {
        const size_t close = line.find(']', index + 1);
        const size_t end = close == std::string::npos || close >= content_end ? content_end : close + 1;
        PushToken(tokens, index, end, SyntaxHighlighter::Style::Keyword);
        if (comment != std::string::npos) {
            PushToken(tokens, comment, line.size(), SyntaxHighlighter::Style::Comment);
        }
        return tokens;
    }

    size_t equals = std::string::npos;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;
    for (size_t current = index; current < content_end; ++current) {
        const char character = line[current];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_double) {
            if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_double = false;
            }
            continue;
        }
        if (in_single) {
            if (character == '\'') {
                in_single = false;
            }
            continue;
        }
        if (character == '"') {
            in_double = true;
            continue;
        }
        if (character == '\'') {
            in_single = true;
            continue;
        }
        if (character == '=') {
            equals = current;
            break;
        }
    }

    if (equals != std::string::npos) {
        size_t key_end = equals;
        while (key_end > index && std::isspace(static_cast<unsigned char>(line[key_end - 1]))) {
            --key_end;
        }
        PushToken(tokens, index, key_end, SyntaxHighlighter::Style::Type);
        PushToken(tokens, key_end, equals + 1, SyntaxHighlighter::Style::Normal);
        index = equals + 1;
    }

    while (index < content_end) {
        index = SkipWhitespace(line, index);
        if (index >= content_end) {
            break;
        }

        if (line[index] == '"' || line[index] == '\'') {
            const size_t end = std::min(ParseQuotedStringEnd(line, index), content_end);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = std::min(ParseNumberEnd(line, index), content_end);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(line[index])) {
            const size_t start = index;
            while (index < content_end && IsTomlBareKeyBody(line[index])) {
                ++index;
            }
            const std::string word = ToLower(line.substr(start, index - start));
            const SyntaxHighlighter::Style style =
                TomlConstants().find(word) != TomlConstants().end()
                    ? SyntaxHighlighter::Style::Keyword
                    : SyntaxHighlighter::Style::Normal;
            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    if (comment != std::string::npos) {
        PushToken(tokens, comment, line.size(), SyntaxHighlighter::Style::Comment);
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeHcl(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (line[index] == '#') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

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
            const std::string lowered = ToLower(word);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            size_t lookahead = SkipWhitespace(line, index);
            if (HclKeywords().find(lowered) != HclKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (HclConstants().find(lowered) != HclConstants().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            } else if (lookahead < line.size() && line[lookahead] == '=') {
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
