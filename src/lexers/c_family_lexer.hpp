#pragma once

#include <string>
#include <vector>

#include "syntax_highlighter.hpp"

namespace textlt::lexers {

std::vector<SyntaxHighlighter::Token> TokenizeCsharp(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeCpp(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeDart(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeGo(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeJava(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeJavascript(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeJson(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeJsx(const std::string& line, bool typescript);
std::vector<SyntaxHighlighter::Token> TokenizeKotlin(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeRust(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeSwift(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeTypescript(const std::string& line);

} // namespace textlt::lexers
