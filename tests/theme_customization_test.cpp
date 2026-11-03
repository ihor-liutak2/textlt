#include <cassert>
#include <map>
#include <string>

#include "ftxui/screen/terminal.hpp"

#include "theme.hpp"

int main() {
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);

    ftxui::Color parsed;
    assert(textlt::ParseThemeColor("#123ABC", parsed));
    assert(parsed == ftxui::Color::RGB(0x12, 0x3A, 0xBC));
    assert(textlt::ThemeColorToString(parsed) == "#123ABC");
    assert(textlt::ParseThemeColor(" cyan ", parsed));
    assert(parsed == ftxui::Color::Cyan);
    assert(!textlt::ParseThemeColor("#12ZZ00", parsed));

    textlt::Theme theme;
    std::string error;
    assert(textlt::ApplyThemeColorOverrides(
        theme,
        {{"background", "#010203"},
         {"syntax_keyword", "yellow"},
         {"button_primary", "#AABBCC"}},
        &error));
    assert(error.empty());
    assert(theme.background == ftxui::Color::RGB(1, 2, 3));
    assert(theme.syntax_keyword == ftxui::Color::Yellow);
    assert(theme.button_primary == ftxui::Color::RGB(0xAA, 0xBB, 0xCC));
    assert(!textlt::ApplyThemeColorOverrides(
        theme, {{"foreground", "invalid"}}, &error));
    assert(!error.empty());

    return 0;
}
