#pragma once

#include <functional>
#include <string>

namespace textlt::connect {

struct BrowserTheme {
    std::string background = "#0000AA";
    std::string foreground = "#FFFFFF";
    std::string input_background = "#000000";
    std::string input_foreground = "#FFFFFF";
    std::string accent = "#00AAAA";
    std::string accent_text = "#000000";
};

using BrowserThemeSource = std::function<BrowserTheme()>;

} // namespace textlt::connect
