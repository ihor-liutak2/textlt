#pragma once

#include <string>
#include <vector>

#include "syntax_highlighter.hpp"

namespace textlt::lexers {

std::vector<SyntaxHighlighter::Token> TokenizeBlade(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeCss(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeLess(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeSass(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeScss(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeSingleFileComponent(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeHtml(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeXml(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizePhp(const std::string& line,
                                                   SyntaxHighlighter::TokenizationContext* context);

} // namespace textlt::lexers
