#pragma once

#include <string>
#include <vector>

#include "syntax_highlighter.hpp"

namespace textlt::lexers {

std::vector<SyntaxHighlighter::Token> TokenizeGraphql(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeSql(const std::string& line);

} // namespace textlt::lexers
