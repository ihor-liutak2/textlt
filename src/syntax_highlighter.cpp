#include "syntax_highlighter.hpp"

#include <algorithm>
#include <cctype>

#include "lexers/c_family_lexer.hpp"
#include "lexers/config_script_lexer.hpp"
#include "lexers/database_lexer.hpp"
#include "lexers/lexer_common.hpp"
#include "lexers/web_lexer.hpp"

namespace textlt {
namespace {

enum class Language {
    Bash,
    Blade,
    Cpp,
    Csharp,
    Css,
    Dockerfile,
    Env,
    Graphql,
    Ini,
    Html,
    Java,
    Javascript,
    Jsx,
    Json,
    Go,
    Php,
    Plain,
    Python,
    Ruby,
    Rust,
    Sql,
    Typescript,
    Tsx,
    Yaml,
};

std::string BaseName(const std::string& path) {
    const size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

std::string ExtensionFromName(const std::string& filename) {
    const size_t dot = filename.find_last_of('.');
    return dot == std::string::npos ? "" : filename.substr(dot);
}

Language LanguageFromPath(const std::string& path) {
    const std::string filename = BaseName(path);
    const std::string extension = ExtensionFromName(filename);

    if (filename.rfind("Dockerfile", 0) == 0) {
        return Language::Dockerfile;
    }
    if (filename == "docker-compose.yml" || filename == "docker-compose.yaml") {
        return Language::Yaml;
    }
    if (filename == "Gemfile") {
        return Language::Ruby;
    }
    if (filename == ".bashrc" || filename == ".profile") {
        return Language::Bash;
    }
    if (filename.size() >= 10 &&
        filename.compare(filename.size() - 10, 10, ".blade.php") == 0) {
        return Language::Blade;
    }
    if (filename == ".env" || filename == ".env.local" ||
        filename == ".env.development" || filename == ".env.production" ||
        (filename.size() >= 4 &&
         filename.compare(filename.size() - 4, 4, ".env") == 0)) {
        return Language::Env;
    }
    if (extension == ".conf" || extension == ".ini") {
        return Language::Ini;
    }
    if (extension == ".cs") {
        return Language::Csharp;
    }
    if (extension == ".css") {
        return Language::Css;
    }
    if (extension == ".gql" || extension == ".graphql") {
        return Language::Graphql;
    }
    if (extension == ".sh" || extension == ".bash" || extension == ".zsh" ||
        extension == ".bashrc" || extension == ".profile") {
        return Language::Bash;
    }
    if (extension == ".go") {
        return Language::Go;
    }
    if (extension == ".html" || extension == ".htm") {
        return Language::Html;
    }
    if (extension == ".js" || extension == ".mjs" || extension == ".cjs") {
        return Language::Javascript;
    }
    if (extension == ".jsx") {
        return Language::Jsx;
    }
    if (extension == ".java") {
        return Language::Java;
    }
    if (extension == ".json") {
        return Language::Json;
    }
    if (extension == ".php") {
        return Language::Php;
    }
    if (extension == ".py") {
        return Language::Python;
    }
    if (extension == ".rb") {
        return Language::Ruby;
    }
    if (extension == ".rs") {
        return Language::Rust;
    }
    if (extension == ".sql") {
        return Language::Sql;
    }
    if (extension == ".ts" || extension == ".mts") {
        return Language::Typescript;
    }
    if (extension == ".tsx") {
        return Language::Tsx;
    }
    if (extension == ".yaml" || extension == ".yml") {
        return Language::Yaml;
    }
    if (extension == ".c" || extension == ".cc" || extension == ".cpp" ||
        extension == ".cxx" || extension == ".h" || extension == ".hh" ||
        extension == ".hpp" || extension == ".hxx") {
        return Language::Cpp;
    }
    return Language::Plain;
}

std::vector<SyntaxHighlighter::Token> TokenizePlain(const std::string& line) {
    std::vector<SyntaxHighlighter::Token> tokens;
    tokens.reserve(1);
    lexers::PushToken(tokens, 0, line.size(), SyntaxHighlighter::Style::Normal);
    return tokens;
}

} // namespace

ftxui::Color SyntaxHighlighter::ColorForStyle(Style style, const Theme& theme) {
    switch (style) {
        case Style::Keyword: return theme.syntax_keyword;
        case Style::Type: return theme.syntax_type;
        case Style::String: return theme.syntax_string;
        case Style::Comment: return theme.syntax_comment;
        case Style::Number: return theme.syntax_number;
        case Style::Normal: return theme.editor_text;
    }
    return theme.editor_text;
}

std::vector<SyntaxHighlighter::Token> SyntaxHighlighter::TokenizeLine(
    const std::string& line,
    const std::string& file_path) {
    return TokenizeLine(line, file_path, nullptr);
}

std::vector<SyntaxHighlighter::Token> SyntaxHighlighter::TokenizeLine(
    const std::string& line,
    const std::string& file_path,
    TokenizationContext* context) {
    switch (LanguageFromPath(file_path)) {
        case Language::Bash:
            return lexers::TokenizeBash(line);
        case Language::Blade:
            return lexers::TokenizeBlade(line);
        case Language::Cpp:
            return lexers::TokenizeCpp(line);
        case Language::Csharp:
            return lexers::TokenizeCsharp(line);
        case Language::Css:
            return lexers::TokenizeCss(line);
        case Language::Dockerfile:
            return lexers::TokenizeDockerfile(line);
        case Language::Env:
            return lexers::TokenizeEnv(line);
        case Language::Graphql:
            return lexers::TokenizeGraphql(line);
        case Language::Go:
            return lexers::TokenizeGo(line);
        case Language::Html:
            return lexers::TokenizeHtml(line);
        case Language::Ini:
            return lexers::TokenizeIni(line);
        case Language::Java:
            return lexers::TokenizeJava(line);
        case Language::Javascript:
            return lexers::TokenizeJavascript(line);
        case Language::Json:
            return lexers::TokenizeJson(line);
        case Language::Jsx:
            return lexers::TokenizeJsx(line, false);
        case Language::Php:
            return lexers::TokenizePhp(line, context);
        case Language::Python:
            return lexers::TokenizePython(line);
        case Language::Ruby:
            return lexers::TokenizeRuby(line);
        case Language::Rust:
            return lexers::TokenizeRust(line);
        case Language::Sql:
            return lexers::TokenizeSql(line);
        case Language::Typescript:
            return lexers::TokenizeTypescript(line);
        case Language::Tsx:
            return lexers::TokenizeJsx(line, true);
        case Language::Yaml:
            return lexers::TokenizeYaml(line);
        case Language::Plain:
            return TokenizePlain(line);
    }
    return TokenizePlain(line);
}

ftxui::Element SyntaxHighlighter::HighlightLine(const std::string& line,
                                                const Theme& theme,
                                                const std::string& file_path) {
    ftxui::Elements elements;
    for (const Token& token : TokenizeLine(line, file_path)) {
        elements.push_back(
            ftxui::text(line.substr(token.start, token.length)) |
            ftxui::color(ColorForStyle(token.style, theme)));
    }
    return ftxui::hbox(std::move(elements));
}

} // namespace textlt
