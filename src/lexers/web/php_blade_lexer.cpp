std::vector<SyntaxHighlighter::Token> TokenizePhpExpression(const std::string& expression) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(expression.size() / 4 + 1);

    size_t index = 0;
    while (index < expression.size()) {
        if (StartsWith(expression, index, "//")) {
            PushToken(tokens, index, expression.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (StartsWith(expression, index, "/*")) {
            const size_t end = FindCommentEnd(expression, index + 2, "*/");
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            index = end;
            continue;
        }

        if (expression[index] == '#') {
            PushToken(tokens, index, expression.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (expression[index] == '"' || expression[index] == '\'' || expression[index] == '`') {
            const size_t end = ParseQuotedStringEnd(expression, index);
            PushToken(tokens, index, end, SyntaxHighlighter::Style::String);
            index = end;
            continue;
        }

        if (expression[index] == '$' && index + 1 < expression.size() &&
            IsIdentifierStart(expression[index + 1])) {
            const size_t start = index;
            index += 2;
            while (index < expression.size() && IsIdentifierBody(expression[index])) {
                ++index;
            }
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Type);
            continue;
        }

        if (IsNumberStart(expression, index)) {
            const size_t start = index;
            index = ParseNumberEnd(expression, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        if (IsIdentifierStart(expression[index])) {
            const size_t start = index;
            while (index < expression.size() && IsIdentifierBody(expression[index])) {
                ++index;
            }
            const std::string word = expression.substr(start, index - start);
            const SyntaxHighlighter::Style style =
                PhpKeywords().find(word) != PhpKeywords().end()
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

std::vector<SyntaxHighlighter::Token> TokenizeBlade(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t ambient_start = 0;
    size_t index = 0;
    while (index < line.size()) {
        const bool escaped_echo = StartsWith(line, index, "{{");
        const bool unescaped_echo = StartsWith(line, index, "{!!");
        if (escaped_echo || unescaped_echo) {
            if (ambient_start < index) {
                PushShiftedTokens(
                    tokens,
                    TokenizeHtml(line.substr(ambient_start, index - ambient_start)),
                    ambient_start);
            }

            const std::string close_marker = escaped_echo ? "}}" : "!!}";
            const size_t open_end = index + (escaped_echo ? 2 : 3);
            const size_t close = line.find(close_marker, open_end);
            const size_t close_start = close == std::string::npos ? line.size() : close;
            const size_t close_end =
                close == std::string::npos ? line.size() : close + close_marker.size();

            PushToken(tokens, index, open_end, SyntaxHighlighter::Style::Normal);
            PushShiftedTokens(
                tokens,
                TokenizePhpExpression(line.substr(open_end, close_start - open_end)),
                open_end);
            if (close_start < close_end) {
                PushToken(tokens, close_start, close_end, SyntaxHighlighter::Style::Normal);
            }

            index = close_end;
            ambient_start = index;
            continue;
        }

        if (line[index] == '@' && index + 1 < line.size() && IsIdentifierStart(line[index + 1])) {
            size_t directive_end = index + 2;
            while (directive_end < line.size() && IsIdentifierBody(line[directive_end])) {
                ++directive_end;
            }

            const std::string directive = line.substr(index + 1, directive_end - index - 1);
            if (BladeDirectives().find(directive) != BladeDirectives().end()) {
                if (ambient_start < index) {
                    PushShiftedTokens(
                        tokens,
                        TokenizeHtml(line.substr(ambient_start, index - ambient_start)),
                        ambient_start);
                }
                PushToken(tokens, index, directive_end, SyntaxHighlighter::Style::Keyword);
                index = directive_end;
                ambient_start = index;
                continue;
            }
        }

        ++index;
    }

    if (ambient_start < line.size()) {
        PushShiftedTokens(
            tokens,
            TokenizeHtml(line.substr(ambient_start)),
            ambient_start);
    }

    return tokens;
}

void PushShiftedTokens(std::vector<SyntaxHighlighter::Token>& tokens,
                       const std::vector<SyntaxHighlighter::Token>& source,
                       size_t offset) {
    for (const SyntaxHighlighter::Token& token : source) {
        PushToken(tokens, offset + token.start, offset + token.start + token.length, token.style);
    }
}


std::vector<SyntaxHighlighter::Token> TokenizePhpNormalLine(
    const std::string& line,
    SyntaxHighlighter::TokenizationContext* context) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (StartsWith(line, index, "<?php")) {
            PushToken(tokens, index, index + 5, SyntaxHighlighter::Style::Keyword);
            index += 5;
            continue;
        }
        if (StartsWith(line, index, "?>")) {
            PushToken(tokens, index, index + 2, SyntaxHighlighter::Style::Keyword);
            index += 2;
            continue;
        }
        if (StartsWith(line, index, "<<<")) {
            std::string heredoc_identifier;
            SyntaxHighlighter::EmbeddedLanguage heredoc_language =
                SyntaxHighlighter::EmbeddedLanguage::None;
            size_t heredoc_end = index;
            if (ParsePhpHeredocStart(
                    line, index, &heredoc_identifier, &heredoc_language, &heredoc_end)) {
                PushToken(tokens, index, heredoc_end, SyntaxHighlighter::Style::String);
                if (context) {
                    context->php_heredoc_identifier = heredoc_identifier;
                    context->php_heredoc_language = heredoc_language;
                }
                index = heredoc_end;
                continue;
            }
        }
        if (line[index] == '/' && index + 1 < line.size() && line[index + 1] == '/') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }
        if (line[index] == '#') {
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
            const size_t start = index;
            index = ParseQuotedStringEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::String);
            continue;
        }
        if (line[index] == '$' && index + 1 < line.size() && IsIdentifierStart(line[index + 1])) {
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
                PhpKeywords().find(word) != PhpKeywords().end()
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

std::vector<SyntaxHighlighter::Token> TokenizePhp(
    const std::string& line,
    SyntaxHighlighter::TokenizationContext* context) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    if (context && context->php_heredoc_language != SyntaxHighlighter::EmbeddedLanguage::None) {
        if (IsLineExactlyIdentifier(line, context->php_heredoc_identifier)) {
            PushToken(tokens, 0, line.size(), SyntaxHighlighter::Style::Keyword);
            context->php_heredoc_language = SyntaxHighlighter::EmbeddedLanguage::None;
            context->php_heredoc_identifier.clear();
            return tokens;
        }

        switch (context->php_heredoc_language) {
            case SyntaxHighlighter::EmbeddedLanguage::Html:
                return TokenizeHtml(line);
            case SyntaxHighlighter::EmbeddedLanguage::Css:
                return TokenizeCss(line);
            case SyntaxHighlighter::EmbeddedLanguage::Javascript:
                return TokenizeJavascript(line);
            case SyntaxHighlighter::EmbeddedLanguage::None:
                break;
        }
    }

    return TokenizePhpNormalLine(line, context);
}
