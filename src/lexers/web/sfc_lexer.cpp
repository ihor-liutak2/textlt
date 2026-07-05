bool LooksLikeScriptLine(const std::string& line, size_t first_text) {
    static const std::unordered_set<std::string> script_words = {
        "await", "const", "export", "function", "if", "import", "interface", "let",
        "return", "type", "var"
    };

    if (first_text >= line.size() || !IsJsIdentifierStart(line[first_text])) {
        return false;
    }

    size_t end = first_text + 1;
    while (end < line.size() && IsJsIdentifierBody(line[end])) {
        ++end;
    }
    return script_words.find(line.substr(first_text, end - first_text)) != script_words.end();
}

bool LooksLikeStyleLine(const std::string& line, size_t first_text) {
    if (first_text >= line.size()) {
        return false;
    }
    if (StartsWith(line, first_text, "//") || StartsWith(line, first_text, "/*")) {
        return true;
    }
    if (line[first_text] == '.' || line[first_text] == '#' || line[first_text] == '$' ||
        line[first_text] == '@') {
        return true;
    }
    const size_t colon = line.find(':', first_text);
    const size_t tag = line.find('<', first_text);
    return colon != std::string::npos &&
        (tag == std::string::npos || colon < tag) &&
        line.find('=', first_text) == std::string::npos;
}

std::vector<SyntaxHighlighter::Token> TokenizeSvelteControlLine(const std::string& line,
                                                                size_t first_text) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(3);
    if (first_text > 0) {
        PushToken(tokens, 0, first_text, SyntaxHighlighter::Style::Normal);
    }
    const size_t close = line.find('}', first_text + 1);
    const size_t end = close == std::string::npos ? line.size() : close + 1;
    PushToken(tokens, first_text, end, SyntaxHighlighter::Style::Keyword);
    if (end < line.size()) {
        PushToken(tokens, end, line.size(), SyntaxHighlighter::Style::Normal);
    }
    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeSingleFileComponent(const std::string& line) {
    const size_t first_text = SkipWhitespace(line, 0);
    if (first_text >= line.size()) {
        return TokenizeHtml(line);
    }

    if (StartsWith(line, first_text, "{#") || StartsWith(line, first_text, "{/")) {
        return TokenizeSvelteControlLine(line, first_text);
    }

    if (line[first_text] == '<' || StartsWith(line, first_text, "---")) {
        return TokenizeHtml(line);
    }

    if (LooksLikeScriptLine(line, first_text)) {
        return TokenizeTypescript(line);
    }

    if (LooksLikeStyleLine(line, first_text)) {
        return TokenizeScss(line);
    }

    return TokenizeHtml(line);
}
