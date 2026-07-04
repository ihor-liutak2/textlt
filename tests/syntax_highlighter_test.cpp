#include "syntax_highlighter.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace {

bool ContainsStyledText(const std::string& line,
                        const std::string& file_path,
                        const std::string& expected_text,
                        textlt::SyntaxHighlighter::Style expected_style) {
    const std::vector<textlt::SyntaxHighlighter::Token> tokens =
        textlt::SyntaxHighlighter::TokenizeLine(line, file_path);
    for (const textlt::SyntaxHighlighter::Token& token : tokens) {
        if (token.style == expected_style &&
            line.substr(token.start, token.length) == expected_text) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    using Style = textlt::SyntaxHighlighter::Style;

    assert(ContainsStyledText("fun greet(name: String) = println(\"Hi\")",
                              "sample.kt",
                              "fun",
                              Style::Keyword));
    assert(ContainsStyledText("fun greet(name: String) = println(\"Hi\")",
                              "sample.kt",
                              "String",
                              Style::Type));

    assert(ContainsStyledText("let greeting: String = \"Hi\"",
                              "sample.swift",
                              "let",
                              Style::Keyword));
    assert(ContainsStyledText("let greeting: String = \"Hi\"",
                              "sample.swift",
                              "String",
                              Style::Type));

    assert(ContainsStyledText("final count = 42;",
                              "sample.dart",
                              "final",
                              Style::Keyword));
    assert(ContainsStyledText("final count = 42;",
                              "sample.dart",
                              "42",
                              Style::Number));

    assert(ContainsStyledText("function Test-It { param([string]$Name) Write-Host $Name }",
                              "sample.ps1",
                              "function",
                              Style::Keyword));
    assert(ContainsStyledText("function Test-It { param([string]$Name) Write-Host $Name }",
                              "sample.ps1",
                              "$Name",
                              Style::Type));

    assert(ContainsStyledText("name = \"TextLT\"",
                              "sample.toml",
                              "name",
                              Style::Type));
    assert(ContainsStyledText("name = \"TextLT\"",
                              "sample.toml",
                              "\"TextLT\"",
                              Style::String));

    assert(ContainsStyledText("resource \"local_file\" \"demo\" {",
                              "main.tf",
                              "resource",
                              Style::Keyword));
    assert(ContainsStyledText("resource \"local_file\" \"demo\" {",
                              "main.tf",
                              "\"local_file\"",
                              Style::String));

    return 0;
}
