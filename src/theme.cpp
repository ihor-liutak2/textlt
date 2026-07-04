#include "theme.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <cstdio> // Used by sscanf for hexadecimal RGB parsing.

#include "json_utils.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif

namespace textlt {
    namespace {
        struct Rgb {
            unsigned int red = 0;
            unsigned int green = 0;
            unsigned int blue = 0;
        };

        struct NamedColor {
            ftxui::Color color;
            Rgb rgb;
        };

        const std::map<std::string, NamedColor>& NamedColors() {
            static const std::map<std::string, NamedColor> colors = {
                {"black", {ftxui::Color::Black, {0, 0, 0}}},
                {"blue", {ftxui::Color::Blue, {0, 0, 170}}},
                {"cyan", {ftxui::Color::Cyan, {0, 170, 170}}},
                {"gray_dark", {ftxui::Color::GrayDark, {85, 85, 85}}},
                {"gray_light", {ftxui::Color::GrayLight, {170, 170, 170}}},
                {"green", {ftxui::Color::Green, {0, 170, 0}}},
                {"red", {ftxui::Color::Red, {170, 0, 0}}},
                {"white", {ftxui::Color::White, {255, 255, 255}}},
                {"yellow", {ftxui::Color::Yellow, {170, 85, 0}}},
            };
            return colors;
        }

        bool TryParseRgb(const std::string& name, Rgb* rgb) {
            if (name.size() == 7 && name[0] == '#') {
                unsigned int red = 0, green = 0, blue = 0;
                if (std::sscanf(name.c_str(), "#%2x%2x%2x", &red, &green, &blue) == 3) {
                    *rgb = {red, green, blue};
                    return true;
                }
            }

            const auto iter = NamedColors().find(name);
            if (iter == NamedColors().end()) {
                return false;
            }
            *rgb = iter->second.rgb;
            return true;
        }

        bool IsLightColor(const std::string& name) {
            Rgb rgb;
            if (!TryParseRgb(name, &rgb)) {
                return false;
            }
            const unsigned int brightness =
                (rgb.red * 299 + rgb.green * 587 + rgb.blue * 114) / 1000;
            return brightness >= 186;
        }

        bool UsesLegacyDarkSyntaxColor(const std::string& color) {
            return color.empty() ||
                color == "#FF79C6" ||
                color == "#8BE9FD" ||
                color == "#F1FA8C" ||
                color == "#BD93F9" ||
                color == "#6272A4";
        }

        void ApplyLightThemeContrastFallbacks(Theme& theme,
                                              const std::string& foreground,
                                              const std::string& menu_foreground,
                                              const std::string& editor_text,
                                              const std::string& syntax_keyword,
                                              const std::string& syntax_type,
                                              const std::string& syntax_string,
                                              const std::string& syntax_number,
                                              const std::string& syntax_comment,
                                              const std::string& gutter,
                                              const std::string& selection_bg,
                                              const std::string& selection_fg) {
            if (foreground.empty()) {
                theme.foreground = ftxui::Color::RGB(26, 26, 26);
            }
            if (menu_foreground.empty()) {
                theme.menu_foreground = ftxui::Color::RGB(45, 45, 45);
            }
            if (editor_text.empty()) {
                theme.editor_text = ftxui::Color::RGB(26, 26, 26);
            }
            if (UsesLegacyDarkSyntaxColor(syntax_keyword)) {
                theme.syntax_keyword = ftxui::Color::RGB(0, 0, 184);
            }
            if (UsesLegacyDarkSyntaxColor(syntax_type)) {
                theme.syntax_type = ftxui::Color::RGB(127, 29, 29);
            }
            if (UsesLegacyDarkSyntaxColor(syntax_string)) {
                theme.syntax_string = ftxui::Color::RGB(11, 93, 30);
            }
            if (UsesLegacyDarkSyntaxColor(syntax_number)) {
                theme.syntax_number = ftxui::Color::RGB(165, 29, 45);
            }
            if (UsesLegacyDarkSyntaxColor(syntax_comment)) {
                theme.syntax_comment = ftxui::Color::RGB(92, 99, 112);
            }
            if (gutter.empty()) {
                theme.gutter = ftxui::Color::RGB(75, 85, 99);
            }
            if (selection_bg.empty()) {
                theme.selection_bg = ftxui::Color::RGB(173, 216, 230);
            }
            if (selection_fg.empty()) {
                theme.selection_fg = ftxui::Color::RGB(26, 26, 26);
            }
        }

        ftxui::Color ColorFromName(const std::string& name, ftxui::Color fallback) {
            // First, accept hexadecimal colors such as #0F2042.
            Rgb rgb;
            if (TryParseRgb(name, &rgb) && name.size() == 7 && name[0] == '#') {
                return ftxui::Color::RGB(static_cast<uint8_t>(rgb.red),
                                         static_cast<uint8_t>(rgb.green),
                                         static_cast<uint8_t>(rgb.blue));
            }

            // Then fall back to standard terminal color names.
            auto iter = NamedColors().find(name);
            return iter == NamedColors().end() ? fallback : iter->second.color;
        }

        Theme LoadThemeFile(const std::filesystem::path& path) {
            Theme theme = FallbackTheme();
            const Json content = LoadJsonObject(path);

            const std::string name = JsonString(content, "name");
            if (!name.empty()) {
                theme.name = name;
            }
            const std::string background = JsonString(content, "background");
            const std::string foreground = JsonString(content, "foreground");
            const std::string menu_foreground = JsonString(content, "menu_foreground");
            const std::string editor_text = JsonString(content, "editor_text");
            const std::string syntax_keyword = JsonString(content, "syntax_keyword");
            const std::string syntax_type = JsonString(content, "syntax_type");
            const std::string syntax_string = JsonString(content, "syntax_string");
            const std::string syntax_number = JsonString(content, "syntax_number");
            const std::string syntax_comment = JsonString(content, "syntax_comment");
            const std::string gutter = JsonString(content, "gutter");
            const std::string selection_bg = JsonString(content, "selection_bg");
            const std::string selection_fg = JsonString(content, "selection_fg");

            theme.background = ColorFromName(background, theme.background);
            theme.foreground = ColorFromName(foreground, theme.foreground);
            theme.header = ColorFromName(JsonString(content, "header"), theme.header);
            theme.menu_background = ColorFromName(
                JsonString(content, "menu_background"), theme.menu_background);
            theme.menu_foreground = ColorFromName(menu_foreground, theme.menu_foreground);
            theme.status = ColorFromName(JsonString(content, "status"), theme.status);
            theme.editor_text = ColorFromName(editor_text, theme.editor_text);
            theme.syntax_keyword = ColorFromName(syntax_keyword, theme.syntax_keyword);
            theme.syntax_type = ColorFromName(syntax_type, theme.syntax_type);
            theme.syntax_string = ColorFromName(syntax_string, theme.syntax_string);
            theme.syntax_number = ColorFromName(syntax_number, theme.syntax_number);
            theme.syntax_comment = ColorFromName(syntax_comment, theme.syntax_comment);
            theme.gutter = ColorFromName(gutter, theme.gutter);
            theme.cursor = ColorFromName(JsonString(content, "cursor"), theme.cursor);
            theme.selection_bg = ColorFromName(selection_bg, theme.selection_bg);
            theme.selection_fg = ColorFromName(selection_fg, theme.selection_fg);
            theme.match_bg = ColorFromName(JsonString(content, "match_bg"), theme.match_bg);
            theme.active_match_bg = ColorFromName(
                JsonString(content, "active_match_bg"), theme.active_match_bg);
            theme.modal_background = ColorFromName(
                JsonString(content, "modal_background"), theme.modal_background);
            theme.modal_foreground = ColorFromName(
                JsonString(content, "modal_foreground"), theme.modal_foreground);
            theme.modal_border = ColorFromName(JsonString(content, "modal_border"), theme.modal_border);
            theme.modal_accent = ColorFromName(JsonString(content, "modal_accent"), theme.modal_accent);
            theme.modal_text_color = ColorFromName(
                JsonString(content, "modal_text_color"), theme.modal_text_color);
            theme.modal_input_bg = ColorFromName(
                JsonString(content, "modal_input_bg"), theme.modal_input_bg);
            theme.modal_input_fg = ColorFromName(
                JsonString(content, "modal_input_fg"), theme.modal_input_fg);
            theme.modal_selected_item_bg = ColorFromName(
                JsonString(content, "modal_selected_item_bg"), theme.modal_selected_item_bg);
            theme.modal_selected_item_fg = ColorFromName(
                JsonString(content, "modal_selected_item_fg"), theme.modal_selected_item_fg);
            if (IsLightColor(background)) {
                ApplyLightThemeContrastFallbacks(theme,
                                                 foreground,
                                                 menu_foreground,
                                                 editor_text,
                                                 syntax_keyword,
                                                 syntax_type,
                                                 syntax_string,
                                                 syntax_number,
                                                 syntax_comment,
                                                 gutter,
                                                 selection_bg,
                                                 selection_fg);
            }
            return theme;
        }

        std::filesystem::path UserThemeDirectory() {
#ifdef _WIN32
            const char* app_data = std::getenv("APPDATA");
            if (app_data && !std::string(app_data).empty()) {
                return std::filesystem::path(app_data) / "textlt" / "themes";
            }

            const char* user_profile = std::getenv("USERPROFILE");
            if (user_profile && !std::string(user_profile).empty()) {
                return std::filesystem::path(user_profile) / "AppData" / "Roaming" /
                       "textlt" / "themes";
            }
            return {};
#else
            const char* home = std::getenv("HOME");
            if (!home || std::string(home).empty()) {
                return {};
            }
            return std::filesystem::path(home) / ".config" / "textlt" / "themes";
#endif
        }

        std::vector<std::filesystem::path> ExecutableThemeDirectories() {
#ifdef _WIN32
            std::string buffer(MAX_PATH, '\0');
            DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            while (length == buffer.size()) {
                buffer.resize(buffer.size() * 2);
                length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            }

            if (length == 0) {
                return {};
            }

            buffer.resize(length);
            const std::filesystem::path executable_path(buffer);
#else
            std::error_code error;
            const std::filesystem::path executable_path =
                std::filesystem::read_symlink("/proc/self/exe", error);
            if (error || executable_path.empty()) {
                return {};
            }
#endif

            const std::filesystem::path executable_directory = executable_path.parent_path();
            return {
                executable_directory / "themes",
                executable_directory.parent_path() / "themes",
                executable_directory.parent_path() / "share" / "textlt" / "themes",
            };
        }

        std::vector<Theme> LoadAvailableThemesFromDirectory(
            const std::filesystem::path& directory) {
            std::vector<Theme> themes;
            if (directory.empty()) {
                return themes;
            }

            std::error_code error;
            if (!std::filesystem::exists(directory, error) ||
                !std::filesystem::is_directory(directory, error)) {
                return themes;
            }

            for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
                if (entry.is_regular_file(error) && entry.path().extension() == ".json") {
                    themes.push_back(LoadThemeFile(entry.path()));
                }
            }
            std::sort(themes.begin(), themes.end(), [](const Theme& left, const Theme& right) {
                return left.name < right.name;
            });
            return themes;
        }

    } // namespace

    std::vector<Theme> LoadThemesFromDirectory(const std::filesystem::path& directory) {
        std::vector<Theme> themes = LoadAvailableThemesFromDirectory(directory);
        return themes.empty() ? std::vector<Theme>{FallbackTheme()} : themes;
    }

    std::vector<Theme> LoadThemesFromConfiguredLocations() {
        std::vector<std::filesystem::path> theme_directories = {UserThemeDirectory()};
        const std::vector<std::filesystem::path> executable_theme_directories =
            ExecutableThemeDirectories();
        theme_directories.insert(theme_directories.end(),
                                 executable_theme_directories.begin(),
                                 executable_theme_directories.end());
        theme_directories.push_back("themes");

        for (const std::filesystem::path& directory : theme_directories) {
            std::vector<Theme> themes = LoadAvailableThemesFromDirectory(directory);
            if (!themes.empty()) {
                return themes;
            }
        }
        return {FallbackTheme()};
    }

    Theme FallbackTheme() {
        return {};
    }

    Theme FindThemeByName(const std::vector<Theme>& themes, const std::string& name) {
        auto iter = std::find_if(themes.begin(), themes.end(), [&](const Theme& theme) {
            return theme.name == name;
        });
        if (iter != themes.end()) {
            return *iter;
        }

        // Missing saved theme names fall back to the canonical light theme
        // before using the built-in default theme.
        auto github_light = std::find_if(themes.begin(), themes.end(), [](const Theme& theme) {
            return theme.name == "GitHub Light";
        });
        if (github_light != themes.end()) {
            return *github_light;
        }

        return FallbackTheme();
    }

} // namespace textlt
