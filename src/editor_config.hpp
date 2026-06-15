#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

struct EditorConfig {
    bool show_line_numbers = true;
    bool show_file_explorer = true;
    bool smart_word_wrap = false;
    bool syntax_highlighting = true;
    bool auto_pairing = true;
    bool auto_indent = true;
    bool search_match_case = false;
    bool search_whole_word = false;
    int tab_size = 4;
    std::string active_theme_name = "Blueprint";
    std::vector<std::string> favorites_;

    bool AddFavorite(const std::string& path);
    bool RemoveFavorite(const std::string& path);
    bool IsFavorite(const std::string& path) const;

    void SetConfigPath(std::filesystem::path path);
    bool Persist() const;

    static std::filesystem::path DefaultConfigPath();
    static std::filesystem::path FallbackConfigPath();
    static std::string NormalizeFavoritePath(const std::string& path);

private:
    std::filesystem::path config_path_;
};

} // namespace textlt
