#include "editor_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include "json_utils.hpp"

namespace textlt {
namespace {

std::filesystem::path UserConfigDirectory() {
#ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && !std::string(app_data).empty()) {
        return std::filesystem::path(app_data) / "textlt";
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile) / "AppData" / "Roaming" / "textlt";
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home) / ".config" / "textlt";
#endif
}

std::filesystem::path UserHomeDirectory() {
#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (!user_profile || std::string(user_profile).empty()) {
        return {};
    }
    return std::filesystem::path(user_profile);
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home);
#endif
}

bool WriteConfigAtomically(const std::filesystem::path& path, const EditorConfig& config) {
    Json root = {
        {"show_line_numbers", config.show_line_numbers},
        {"show_file_explorer", config.show_file_explorer},
        {"smart_word_wrap", config.smart_word_wrap},
        {"syntax_highlighting", config.syntax_highlighting},
        {"auto_pairing", config.auto_pairing},
        {"auto_indent", config.auto_indent},
        {"search_match_case", config.search_match_case},
        {"search_whole_word", config.search_whole_word},
        {"tab_size", config.tab_size},
        {"active_theme_name", config.active_theme_name},
        {"favorites_", Json::array()},
    };
    for (const FavoriteEntry& favorite : config.favorites_) {
        root["favorites_"].push_back({
            {"path", favorite.path},
            {"row", favorite.row},
            {"column", favorite.column},
        });
    }
    return WriteJsonAtomically(path, root);
}

} // namespace

bool EditorConfig::AddFavorite(const std::string& path) {
    const std::string normalized_path = NormalizeFavoritePath(path);
    if (normalized_path.empty() || IsFavorite(normalized_path)) {
        return false;
    }

    favorites_.push_back({normalized_path, 0, 0});
    Persist();
    return true;
}

bool EditorConfig::RemoveFavorite(const std::string& path) {
    const std::string normalized_path = NormalizeFavoritePath(path);
    if (normalized_path.empty()) {
        return false;
    }

    const size_t old_size = favorites_.size();
    favorites_.erase(
        std::remove_if(
            favorites_.begin(),
            favorites_.end(),
            [&](const FavoriteEntry& favorite) {
                return favorite.path == normalized_path;
            }),
        favorites_.end());
    if (favorites_.size() == old_size) {
        return false;
    }

    Persist();
    return true;
}

bool EditorConfig::IsFavorite(const std::string& path) const {
    const std::string normalized_path = NormalizeFavoritePath(path);
    if (normalized_path.empty()) {
        return false;
    }
    return FindFavorite(normalized_path) != nullptr;
}

const FavoriteEntry* EditorConfig::FindFavorite(const std::string& path) const {
    const std::string normalized_path = NormalizeFavoritePath(path);
    if (normalized_path.empty()) {
        return nullptr;
    }

    auto iter = std::find_if(
        favorites_.begin(),
        favorites_.end(),
        [&](const FavoriteEntry& favorite) {
            return favorite.path == normalized_path;
        });
    return iter == favorites_.end() ? nullptr : &*iter;
}

bool EditorConfig::UpdateFavoriteCursor(const std::string& path, size_t row, size_t column) {
    const std::string normalized_path = NormalizeFavoritePath(path);
    if (normalized_path.empty()) {
        return false;
    }

    auto iter = std::find_if(
        favorites_.begin(),
        favorites_.end(),
        [&](const FavoriteEntry& favorite) {
            return favorite.path == normalized_path;
        });
    if (iter == favorites_.end()) {
        return false;
    }

    iter->row = row;
    iter->column = column;
    return Persist();
}

bool EditorConfig::SetActiveThemeName(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    active_theme_name = name;
    return Persist();
}

void EditorConfig::SetConfigPath(std::filesystem::path path) {
    config_path_ = std::move(path);
}

bool EditorConfig::Persist() const {
    const std::filesystem::path primary_path =
        config_path_.empty() ? DefaultConfigPath() : config_path_;
    if (WriteConfigAtomically(primary_path, *this)) {
        return true;
    }

    const std::filesystem::path fallback_path = FallbackConfigPath();
    if (!fallback_path.empty() && fallback_path != primary_path) {
        return WriteConfigAtomically(fallback_path, *this);
    }
    return false;
}

std::filesystem::path EditorConfig::DefaultConfigPath() {
    const std::filesystem::path config_directory = UserConfigDirectory();
    if (config_directory.empty()) {
        return "config.json";
    }
    return config_directory / "config.json";
}

std::filesystem::path EditorConfig::FallbackConfigPath() {
    const std::filesystem::path home = UserHomeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / ".textlt_config.json";
}

std::string EditorConfig::NormalizeFavoritePath(const std::string& path) {
    if (path.empty() || path == "Untitled" || path == "untitled.txt") {
        return {};
    }

    std::error_code error;
    std::filesystem::path normalized_path =
        std::filesystem::absolute(std::filesystem::path(path), error);
    if (error) {
        return {};
    }

    normalized_path = std::filesystem::weakly_canonical(normalized_path, error);
    if (error) {
        normalized_path = normalized_path.lexically_normal();
    }
    return normalized_path.string();
}

} // namespace textlt
