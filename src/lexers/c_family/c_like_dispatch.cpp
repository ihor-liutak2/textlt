std::vector<SyntaxHighlighter::Token> TokenizeCLikeLine(const std::string& line,
                                                        const std::string& family) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(line.size() / 4 + 1);

    size_t index = 0;
    while (index < line.size()) {
        if (family == "cpp" && IsPreprocessorStart(line, index)) {
            const size_t directive_start = index;
            const size_t directive_end = ParsePreprocessorDirectiveEnd(line, index);
            PushToken(tokens, directive_start, directive_end, SyntaxHighlighter::Style::Keyword);

            const std::string directive =
                line.substr(directive_start + 1, directive_end - directive_start - 1);
            if (directive == "include" || directive == "import") {
                const size_t target_start = SkipWhitespace(line, directive_end);
                if (target_start > directive_end) {
                    PushToken(tokens, directive_end, target_start, SyntaxHighlighter::Style::Normal);
                }
                const size_t target_end = ParseIncludeTargetEnd(line, target_start);
                if (target_end > target_start) {
                    PushToken(tokens, target_start, target_end, SyntaxHighlighter::Style::String);
                    index = target_end;
                    continue;
                }
            }

            index = directive_end;
            continue;
        }

        if (line[index] == '/' && index + 1 < line.size() && line[index + 1] == '/') {
            PushToken(tokens, index, line.size(), SyntaxHighlighter::Style::Comment);
            break;
        }

        if (StartsWith(line, index, "/*")) {
            const size_t end = FindCommentEnd(line, index + 2, "*/");
            PushToken(tokens, index, end, SyntaxHighlighter::Style::Comment);
            index = end;
            continue;
        }

        if (line[index] == '"' ||
            (family != "json" && (line[index] == '\'' || line[index] == '`'))) {
            const size_t start = index;
            index = ParseQuotedStringEnd(line, index);
            const SyntaxHighlighter::Style style =
                family == "json" && IsJsonKey(line, index)
                    ? SyntaxHighlighter::Style::Type
                    : SyntaxHighlighter::Style::String;
            PushToken(tokens, start, index, style);
            continue;
        }

        if (IsNumberStart(line, index)) {
            const size_t start = index;
            index = ParseNumberEnd(line, index);
            PushToken(tokens, start, index, SyntaxHighlighter::Style::Number);
            continue;
        }

        const bool is_identifier_start =
            (family == "javascript" || family == "typescript")
                ? IsJsIdentifierStart(line[index])
                : IsIdentifierStart(line[index]);
        if (is_identifier_start) {
            const size_t start = index;
            if (family == "cpp") {
                while (index < line.size() && IsCppWordBody(line[index])) {
                    ++index;
                }
            } else if (family == "javascript" || family == "typescript") {
                while (index < line.size() && IsJsIdentifierBody(line[index])) {
                    ++index;
                }
            } else {
                while (index < line.size() && IsIdentifierBody(line[index])) {
                    ++index;
                }
            }

            const std::string word = line.substr(start, index - start);
            SyntaxHighlighter::Style style = SyntaxHighlighter::Style::Normal;
            if (family == "cpp") {
                if (CppKeywords().find(word) != CppKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (CppTypes().find(word) != CppTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "java") {
                if (JavaKeywords().find(word) != JavaKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (JavaTypes().find(word) != JavaTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "javascript") {
                if (JsKeywords().find(word) != JsKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (JsTypes().find(word) != JsTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "typescript") {
                if (TypescriptKeywords().find(word) != TypescriptKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (TypescriptTypes().find(word) != TypescriptTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "kotlin") {
                if (KotlinKeywords().find(word) != KotlinKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (KotlinTypes().find(word) != KotlinTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "swift") {
                if (SwiftKeywords().find(word) != SwiftKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (SwiftTypes().find(word) != SwiftTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if (family == "dart") {
                if (DartKeywords().find(word) != DartKeywords().end()) {
                    style = SyntaxHighlighter::Style::Keyword;
                } else if (DartTypes().find(word) != DartTypes().end()) {
                    style = SyntaxHighlighter::Style::Type;
                }
            } else if ((family == "json" || family == "jsonc") &&
                       JsonKeywords().find(word) != JsonKeywords().end()) {
                style = SyntaxHighlighter::Style::Keyword;
            }

            PushToken(tokens, start, index, style);
            continue;
        }

        PushToken(tokens, index, index + 1, SyntaxHighlighter::Style::Normal);
        ++index;
    }

    return tokens;
}

std::vector<SyntaxHighlighter::Token> TokenizeCpp(const std::string& line) {
    return TokenizeCLikeLine(line, "cpp");
}

std::vector<SyntaxHighlighter::Token> TokenizeJava(const std::string& line) {
    return TokenizeCLikeLine(line, "java");
}

std::vector<SyntaxHighlighter::Token> TokenizeJavascript(const std::string& line) {
    return TokenizeJsLikeLine(line, false);
}

std::vector<SyntaxHighlighter::Token> TokenizeKotlin(const std::string& line) {
    return TokenizeCLikeLine(line, "kotlin");
}

std::vector<SyntaxHighlighter::Token> TokenizeDart(const std::string& line) {
    return TokenizeCLikeLine(line, "dart");
}

std::vector<SyntaxHighlighter::Token> TokenizeSwift(const std::string& line) {
    return TokenizeCLikeLine(line, "swift");
}

std::vector<SyntaxHighlighter::Token> TokenizeTypescript(const std::string& line) {
    return TokenizeJsLikeLine(line, true);
}

std::vector<SyntaxHighlighter::Token> TokenizeJson(const std::string& line) {
    return TokenizeCLikeLine(line, "json");
}

std::vector<SyntaxHighlighter::Token> TokenizeJsonc(const std::string& line) {
    return TokenizeCLikeLine(line, "jsonc");
}
