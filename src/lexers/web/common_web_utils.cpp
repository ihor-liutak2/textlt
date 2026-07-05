const std::unordered_set<std::string>& PhpKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "function",
        "class",
        "public",
        "private",
        "protected",
        "return",
        "if",
        "else",
        "elseif",
        "foreach",
        "as",
        "while",
        "switch",
        "case",
        "echo",
        "include",
        "require",
        "namespace",
        "use",
        "array",
    };
    return keywords;
}

const std::unordered_set<std::string>& BladeDirectives() {
    static const std::unordered_set<std::string> directives = {
        "if",
        "else",
        "elseif",
        "endif",
        "foreach",
        "endforeach",
        "forelse",
        "empty",
        "endforelse",
        "for",
        "endfor",
        "while",
        "endwhile",
        "extends",
        "section",
        "endsection",
        "yield",
        "include",
        "auth",
        "endauth",
        "guest",
        "endguest",
        "vite",
        "livewire",
    };
    return directives;
}

SyntaxHighlighter::EmbeddedLanguage EmbeddedLanguageFromIdentifier(
    const std::string& identifier) {
    const std::string upper = ToUpper(identifier);
    if (upper == "HTML") {
        return SyntaxHighlighter::EmbeddedLanguage::Html;
    }
    if (upper == "CSS") {
        return SyntaxHighlighter::EmbeddedLanguage::Css;
    }
    if (upper == "JS" || upper == "JAVASCRIPT") {
        return SyntaxHighlighter::EmbeddedLanguage::Javascript;
    }
    return SyntaxHighlighter::EmbeddedLanguage::None;
}

bool ParsePhpHeredocStart(const std::string& line,
                          size_t index,
                          std::string* identifier,
                          SyntaxHighlighter::EmbeddedLanguage* language,
                          size_t* end) {
    if (!StartsWith(line, index, "<<<")) {
        return false;
    }

    size_t current = SkipWhitespace(line, index + 3);
    char quote = '\0';
    if (current < line.size() && (line[current] == '\'' || line[current] == '"')) {
        quote = line[current];
        ++current;
    }

    if (current >= line.size() || !IsIdentifierStart(line[current])) {
        return false;
    }

    const size_t identifier_start = current;
    while (current < line.size() && IsIdentifierBody(line[current])) {
        ++current;
    }

    if (quote != '\0') {
        if (current >= line.size() || line[current] != quote) {
            return false;
        }
        ++current;
    }

    const std::string parsed_identifier =
        line.substr(identifier_start, current - identifier_start - (quote != '\0' ? 1 : 0));
    const SyntaxHighlighter::EmbeddedLanguage parsed_language =
        EmbeddedLanguageFromIdentifier(parsed_identifier);
    if (parsed_language == SyntaxHighlighter::EmbeddedLanguage::None) {
        return false;
    }

    *identifier = parsed_identifier;
    *language = parsed_language;
    *end = current;
    return true;
}

void PushShiftedTokens(std::vector<SyntaxHighlighter::Token>& tokens,
                       const std::vector<SyntaxHighlighter::Token>& source,
                       size_t offset);
