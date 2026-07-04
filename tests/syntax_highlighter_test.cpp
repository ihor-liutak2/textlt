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

    assert(ContainsStyledText("// JSON with comments",
                              "sample.jsonc",
                              "// JSON with comments",
                              Style::Comment));
    assert(ContainsStyledText("{ \"enabled\": true }",
                              "sample.jsonc",
                              "true",
                              Style::Keyword));

    assert(ContainsStyledText("$accent: #ff9d00;",
                              "sample.scss",
                              "$accent",
                              Style::Type));
    assert(ContainsStyledText("@accent: #ff9d00;",
                              "sample.less",
                              "@accent",
                              Style::Type));
    assert(ContainsStyledText("color: $accent",
                              "sample.sass",
                              "color",
                              Style::Keyword));

    assert(ContainsStyledText("<template><h1>{{ title }}</h1></template>",
                              "sample.vue",
                              "template",
                              Style::Keyword));
    assert(ContainsStyledText("{#if ready}",
                              "sample.svelte",
                              "{#if ready}",
                              Style::Keyword));
    assert(ContainsStyledText("const title = \"TextLT\";",
                              "sample.astro",
                              "const",
                              Style::Keyword));

    assert(ContainsStyledText("textlt: main.cpp",
                              "Makefile",
                              "textlt",
                              Style::Keyword));
    assert(ContainsStyledText("CXXFLAGS := -std=c++17",
                              "Makefile",
                              "CXXFLAGS",
                              Style::Type));


    assert(ContainsStyledText("result <- function(x) x + 1",
                              "sample.R",
                              "function",
                              Style::Keyword));
    assert(ContainsStyledText("result <- data.frame(value = TRUE)",
                              "sample.R",
                              "TRUE",
                              Style::Type));

    assert(ContainsStyledText("function greet(name::String)",
                              "sample.jl",
                              "function",
                              Style::Keyword));
    assert(ContainsStyledText("function greet(name::String)",
                              "sample.jl",
                              "String",
                              Style::Type));

    assert(ContainsStyledText("const allocator = std.heap.page_allocator;",
                              "sample.zig",
                              "const",
                              Style::Keyword));
    assert(ContainsStyledText("fn main() void {",
                              "sample.zig",
                              "void",
                              Style::Type));

    assert(ContainsStyledText("my $name = 'TextLT';",
                              "sample.pl",
                              "my",
                              Style::Keyword));
    assert(ContainsStyledText("my $name = 'TextLT';",
                              "sample.pl",
                              "$name",
                              Style::Type));

    assert(ContainsStyledText("object Main extends App",
                              "sample.scala",
                              "object",
                              Style::Keyword));
    assert(ContainsStyledText("val name: String = \"TextLT\"",
                              "sample.scala",
                              "String",
                              Style::Type));

    assert(ContainsStyledText("defmodule TextLT.Demo do",
                              "sample.ex",
                              "defmodule",
                              Style::Keyword));
    assert(ContainsStyledText("{:ok, value}",
                              "sample.ex",
                              ":ok",
                              Style::Type));

    assert(ContainsStyledText("case Value of",
                              "sample.erl",
                              "case",
                              Style::Keyword));
    assert(ContainsStyledText("case Value of",
                              "sample.erl",
                              "Value",
                              Style::Type));

    assert(ContainsStyledText("mov eax, 1 ; exit",
                              "sample.asm",
                              "mov",
                              Style::Keyword));
    assert(ContainsStyledText("mov eax, 1 ; exit",
                              "sample.asm",
                              "eax",
                              Style::Type));

    assert(ContainsStyledText("function y = square(x)",
                              "sample.m",
                              "function",
                              Style::Keyword));
    assert(ContainsStyledText("plot(x, y) % draw chart",
                              "sample.m",
                              "% draw chart",
                              Style::Comment));

    return 0;
}
