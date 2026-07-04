#pragma once

#include <string>
#include <vector>

#include "syntax_highlighter.hpp"

namespace textlt::lexers {

std::vector<SyntaxHighlighter::Token> TokenizeBash(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeCMake(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeDockerfile(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeEnv(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeHcl(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeIni(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeLua(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeMakefile(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeMarkdown(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizePowershell(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizePython(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeRuby(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeToml(const std::string& line);
std::vector<SyntaxHighlighter::Token> TokenizeYaml(const std::string& line);

} // namespace textlt::lexers
