#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
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
    ftxui::Color syntax_keyword = ftxui::Color::RGB(255, 121, 198);
    ftxui::Color syntax_type = ftxui::Color::RGB(139, 233, 253);
    ftxui::Color syntax_string = ftxui::Color::RGB(241, 250, 140);
    ftxui::Color syntax_number = ftxui::Color::RGB(189, 147, 249);
    ftxui::Color syntax_comment = ftxui::Color::RGB(98, 114, 164);
    ftxui::Color gutter = ftxui::Color::GrayLight;
    ftxui::Color cursor = ftxui::Color::Cyan;
    ftxui::Color selection_bg = ftxui::Color::Cyan;
    ftxui::Color selection_fg = ftxui::Color::Black;
    ftxui::Color match_bg = ftxui::Color::RGB(74, 112, 215);
    ftxui::Color active_match_bg = ftxui::Color::RGB(204, 238, 0);
    ftxui::Color modal_background = ftxui::Color::Blue;
    ftxui::Color modal_foreground = ftxui::Color::White;
    ftxui::Color modal_border = ftxui::Color::Cyan;
    ftxui::Color modal_accent = ftxui::Color::Cyan;
    ftxui::Color modal_text_color = ftxui::Color::White;
    ftxui::Color modal_input_bg = ftxui::Color::Black;
    ftxui::Color modal_input_fg = ftxui::Color::White;
    ftxui::Color modal_selected_item_bg = ftxui::Color::Cyan;
    ftxui::Color modal_selected_item_fg = ftxui::Color::Black;

    bool IsLight() const {
        return name.find("Light") != std::string::npos;
    }

    ftxui::Color InputForeground() const {
        if (IsLight()) {
            return ftxui::Color::RGB(28, 28, 28);
        }
        return modal_input_fg;
    }

    ftxui::Element InputTransform(ftxui::InputState state) const {
        state.element |= ftxui::bgcolor(modal_input_bg);
        state.element |= ftxui::color(InputForeground());
        return state.element;
    }
};

std::vector<Theme> LoadThemesFromDirectory(const std::filesystem::path& directory);
std::vector<Theme> LoadThemesFromConfiguredLocations();
Theme FallbackTheme();
Theme FindThemeByName(const std::vector<Theme>& themes, const std::string& name);

} // namespace textlt
