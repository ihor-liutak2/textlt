#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ftxui/screen/color.hpp"

namespace textlt {

struct Theme {
    std::string name = "Blueprint";
    ftxui::Color background = ftxui::Color::Blue;
    ftxui::Color foreground = ftxui::Color::White;
    ftxui::Color header = ftxui::Color::Cyan;
    ftxui::Color menu_background = ftxui::Color::Blue;
    ftxui::Color menu_foreground = ftxui::Color::White;
    ftxui::Color status = ftxui::Color::White;
    ftxui::Color editor_text = ftxui::Color::White;
    ftxui::Color gutter = ftxui::Color::GrayLight;
    ftxui::Color cursor = ftxui::Color::Cyan;
    ftxui::Color selection_bg = ftxui::Color::Cyan;
    ftxui::Color selection_fg = ftxui::Color::Black;
    ftxui::Color modal_background = ftxui::Color::Blue;
    ftxui::Color modal_foreground = ftxui::Color::White;
    ftxui::Color modal_border = ftxui::Color::Cyan;
    ftxui::Color modal_accent = ftxui::Color::Cyan;
    ftxui::Color modal_text_color = ftxui::Color::White;
    ftxui::Color modal_input_bg = ftxui::Color::Black;
    ftxui::Color modal_input_fg = ftxui::Color::White;
    ftxui::Color modal_selected_item_bg = ftxui::Color::Cyan;
    ftxui::Color modal_selected_item_fg = ftxui::Color::Black;
};

std::vector<Theme> LoadThemesFromDirectory(const std::filesystem::path& directory);
Theme FallbackTheme();
Theme FindThemeByName(const std::vector<Theme>& themes, const std::string& name);

} // namespace textlt
