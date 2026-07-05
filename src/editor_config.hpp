#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

struct FavoriteEntry {
    std::string path;
    size_t row = 0;
    size_t column = 0;
};

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
    std::string tts_audio_player_id = "auto";
    std::string tts_audio_player_command;
    std::string tts_player_voice_id;
    std::vector<FavoriteEntry> favorites_;
    std::vector<std::string> file_modal_directories_;

    bool AddFavorite(const std::string& path);
    bool RemoveFavorite(const std::string& path);
    bool IsFavorite(const std::string& path) const;
    const FavoriteEntry* FindFavorite(const std::string& path) const;
    bool UpdateFavoriteCursor(const std::string& path, size_t row, size_t column);
    bool SetActiveThemeName(const std::string& name);

    bool AddFileModalDirectory(const std::string& path);
    bool RemoveFileModalDirectory(const std::string& path);
    bool IsFileModalDirectory(const std::string& path) const;

    void SetConfigPath(std::filesystem::path path);
    bool Persist() const;

    static std::filesystem::path DefaultConfigPath();
    static std::filesystem::path FallbackConfigPath();
    static std::string NormalizeFavoritePath(const std::string& path);
    static std::string NormalizeDirectoryPath(const std::string& path);

private:
    std::filesystem::path config_path_;
};

} // namespace textlt
