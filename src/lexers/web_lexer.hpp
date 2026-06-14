#pragma once

#include <string>
#include <vector>

#include "../syntax_highlighter.hpp"

namespace textlt::lexers {

std::vector<SyntaxHighlighter::Token> TokenizeBlade(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeCss(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeHtml(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizePhp(const std::string& line,
                                                   SyntaxHighlighter::TokenizationContext* context);

} // namespace textlt::lexers
