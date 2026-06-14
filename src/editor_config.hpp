#pragma once

#include <string>

namespace textlt {

struct EditorConfig {
    bool show_line_numbers = true;
    bool show_file_explorer = true;
    bool smart_word_wrap = false;
    bool syntax_highlighting = true;
    bool auto_pairing = true;
    bool auto_indent = true;
    int tab_size = 4;
    std::string active_theme_name = "Blueprint";
};

} // namespace textlt
