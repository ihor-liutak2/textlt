#pragma once

#include <string>

namespace textlt {

struct EditorConfig {
    bool show_line_numbers = true;
    bool show_file_explorer = true;
    bool smart_word_wrap = false;
    std::string active_theme_name = "Blueprint";
};

} // namespace textlt
