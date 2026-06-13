#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt {

class SyntaxHighlighter {
public:
    enum class Style {
        Normal,
        Keyword,
        Type,
        String,
        Comment,
        Number,
    };

    enum class EmbeddedLanguage {
        None,
        Html,
        Css,
        Javascript,
    };

    struct Token {
        size_t start = 0;
        size_t length = 0;
        Style style = Style::Normal;
    };

    struct TokenizationContext {
        EmbeddedLanguage php_heredoc_language = EmbeddedLanguage::None;
        std::string php_heredoc_identifier;
    };

    static ftxui::Element HighlightLine(const std::string& line,
                                        const Theme& theme,
                                        const std::string& file_path);
    static std::vector<Token> TokenizeLine(const std::string& line,
                                           const std::string& file_path);
    static std::vector<Token> TokenizeLine(const std::string& line,
                                           const std::string& file_path,
                                           TokenizationContext* context);
    static ftxui::Color ColorForStyle(Style style, const Theme& theme);
};

} // namespace textlt
