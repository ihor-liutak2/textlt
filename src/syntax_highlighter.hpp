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

    struct Token {
        size_t start = 0;
        size_t length = 0;
        Style style = Style::Normal;
    };

    static ftxui::Element HighlightLine(const std::string& line,
                                        const Theme& theme,
                                        const std::string& file_path);
    static std::vector<Token> TokenizeLine(const std::string& line,
                                           const std::string& file_path);
    static ftxui::Color ColorForStyle(Style style, const Theme& theme);
};

} // namespace textlt
