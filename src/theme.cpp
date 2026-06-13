#include "theme.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <cstdio> // Для sscanf

namespace textlt {
    namespace {

        std::string ExtractString(const std::string& content, const std::string& key) {
            const std::string token = "\"" + key + "\"";
            size_t key_pos = content.find(token);
            if (key_pos == std::string::npos) {
                return "";
            }
            size_t colon = content.find(':', key_pos + token.size());
            size_t first_quote = content.find('"', colon);
            size_t second_quote = content.find('"', first_quote + 1);
            if (colon == std::string::npos || first_quote == std::string::npos ||
                second_quote == std::string::npos) {
                return "";
                }
                return content.substr(first_quote + 1, second_quote - first_quote - 1);
        }

        ftxui::Color ColorFromName(const std::string& name, ftxui::Color fallback) {
            // 1. Перевірка на HEX-формат (наприклад, #0F2042)
            if (name.size() == 7 && name[0] == '#') {
                unsigned int r = 0, g = 0, b = 0;
                if (std::sscanf(name.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
                    return ftxui::Color::RGB(static_cast<uint8_t>(r),
                                             static_cast<uint8_t>(g),
                                             static_cast<uint8_t>(b));
                }
            }

            // 2. Стандартні назви кольорів термінала
            static const std::map<std::string, ftxui::Color> colors = {
                {"black", ftxui::Color::Black},
                {"blue", ftxui::Color::Blue},
                {"cyan", ftxui::Color::Cyan},
                {"gray_dark", ftxui::Color::GrayDark},
                {"gray_light", ftxui::Color::GrayLight},
                {"green", ftxui::Color::Green},
                {"red", ftxui::Color::Red},
                {"white", ftxui::Color::White},
                {"yellow", ftxui::Color::Yellow},
            };
            auto iter = colors.find(name);
            return iter == colors.end() ? fallback : iter->second;
        }

        Theme LoadThemeFile(const std::filesystem::path& path) {
            Theme theme = FallbackTheme();
            std::ifstream file(path);
            std::string content((std::istreambuf_iterator<char>(file)), {});

            const std::string name = ExtractString(content, "name");
            if (!name.empty()) {
                theme.name = name;
            }
            theme.background = ColorFromName(ExtractString(content, "background"), theme.background);
            theme.foreground = ColorFromName(ExtractString(content, "foreground"), theme.foreground);
            theme.header = ColorFromName(ExtractString(content, "header"), theme.header);
            theme.menu_background = ColorFromName(
                ExtractString(content, "menu_background"), theme.menu_background);
            theme.menu_foreground = ColorFromName(
                ExtractString(content, "menu_foreground"), theme.menu_foreground);
            theme.status = ColorFromName(ExtractString(content, "status"), theme.status);
            theme.editor_text = ColorFromName(ExtractString(content, "editor_text"), theme.editor_text);
            theme.syntax_keyword = ColorFromName(
                ExtractString(content, "syntax_keyword"), theme.syntax_keyword);
            theme.syntax_type = ColorFromName(
                ExtractString(content, "syntax_type"), theme.syntax_type);
            theme.syntax_string = ColorFromName(
                ExtractString(content, "syntax_string"), theme.syntax_string);
            theme.syntax_number = ColorFromName(
                ExtractString(content, "syntax_number"), theme.syntax_number);
            theme.syntax_comment = ColorFromName(
                ExtractString(content, "syntax_comment"), theme.syntax_comment);
            theme.gutter = ColorFromName(ExtractString(content, "gutter"), theme.gutter);
            theme.cursor = ColorFromName(ExtractString(content, "cursor"), theme.cursor);
            theme.selection_bg = ColorFromName(
                ExtractString(content, "selection_bg"), theme.selection_bg);
            theme.selection_fg = ColorFromName(
                ExtractString(content, "selection_fg"), theme.selection_fg);
            theme.match_bg = ColorFromName(ExtractString(content, "match_bg"), theme.match_bg);
            theme.active_match_bg = ColorFromName(
                ExtractString(content, "active_match_bg"), theme.active_match_bg);
            theme.modal_background = ColorFromName(
                ExtractString(content, "modal_background"), theme.modal_background);
            theme.modal_foreground = ColorFromName(
                ExtractString(content, "modal_foreground"), theme.modal_foreground);
            theme.modal_border = ColorFromName(ExtractString(content, "modal_border"), theme.modal_border);
            theme.modal_accent = ColorFromName(ExtractString(content, "modal_accent"), theme.modal_accent);
            theme.modal_text_color = ColorFromName(
                ExtractString(content, "modal_text_color"), theme.modal_text_color);
            theme.modal_input_bg = ColorFromName(
                ExtractString(content, "modal_input_bg"), theme.modal_input_bg);
            theme.modal_input_fg = ColorFromName(
                ExtractString(content, "modal_input_fg"), theme.modal_input_fg);
            theme.modal_selected_item_bg = ColorFromName(
                ExtractString(content, "modal_selected_item_bg"), theme.modal_selected_item_bg);
            theme.modal_selected_item_fg = ColorFromName(
                ExtractString(content, "modal_selected_item_fg"), theme.modal_selected_item_fg);
            return theme;
        }

    } // namespace

    std::vector<Theme> LoadThemesFromDirectory(const std::filesystem::path& directory) {
        std::vector<Theme> themes;
        std::error_code error;
        if (!std::filesystem::exists(directory, error)) {
            return {FallbackTheme()};
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (entry.is_regular_file(error) && entry.path().extension() == ".json") {
                themes.push_back(LoadThemeFile(entry.path()));
            }
        }
        std::sort(themes.begin(), themes.end(), [](const Theme& left, const Theme& right) {
            return left.name < right.name;
        });
        return themes.empty() ? std::vector<Theme>{FallbackTheme()} : themes;
    }

    Theme FallbackTheme() {
        return {};
    }

    Theme FindThemeByName(const std::vector<Theme>& themes, const std::string& name) {
        auto iter = std::find_if(themes.begin(), themes.end(), [&](const Theme& theme) {
            return theme.name == name;
        });
        return iter == themes.end() ? FallbackTheme() : *iter;
    }

} // namespace textlt
