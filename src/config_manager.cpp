#include "config_manager.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include "json_utils.hpp"

namespace textlt {
namespace {

std::vector<std::string> ExtractStringArray(const Json& root, const char* key) {
    std::vector<std::string> values;
    const auto iter = root.find(key);
    if (iter == root.end() || !iter->is_array()) {
        return values;
    }
    for (const Json& value : *iter) {
        if (value.is_string()) {
            values.push_back(value.get<std::string>());
        }
    }
    return values;
}

std::vector<FavoriteEntry> ExtractFavoriteEntries(const Json& root, const char* key) {
    std::vector<FavoriteEntry> favorites;
    const auto iter = root.find(key);
    if (iter == root.end() || !iter->is_array()) {
        return favorites;
    }

    for (const Json& object : *iter) {
        if (!object.is_object()) {
            continue;
        }
        const std::string path = EditorConfig::NormalizeFavoritePath(
            JsonString(object, "path"));
        if (path.empty()) {
            continue;
        }
        FavoriteEntry favorite;
        favorite.path = path;
        favorite.row = JsonSize(object, "row", 0);
        favorite.column = JsonSize(object, "column", 0);
        favorites.push_back(std::move(favorite));
    }

    return favorites;
}

std::filesystem::path ResolveConfigPath(const std::filesystem::path& requested_path) {
    const bool use_default_path =
        requested_path.empty() ||
        (requested_path.filename() == "config.json" && !requested_path.has_parent_path());
    if (!use_default_path) {
        return requested_path;
    }

    const std::filesystem::path fallback_path = EditorConfig::FallbackConfigPath();
    const std::filesystem::path primary_path = EditorConfig::DefaultConfigPath();
    if (std::filesystem::exists(primary_path)) {
        return primary_path;
    }
    if (!fallback_path.empty() && std::filesystem::exists(fallback_path)) {
        return fallback_path;
    }
    return primary_path;
}

} // namespace

ConfigManager::ConfigManager(std::filesystem::path path)
    : path_(ResolveConfigPath(path)) {}

EditorConfig ConfigManager::Load() const {
    EditorConfig config;
    config.SetConfigPath(path_);
    const Json root = LoadJsonObject(path_);
    config.show_line_numbers = JsonBool(root, "show_line_numbers", config.show_line_numbers);
    config.show_file_explorer = JsonBool(
        root, "show_file_explorer", config.show_file_explorer);
    config.smart_word_wrap = JsonBool(root, "smart_word_wrap", config.smart_word_wrap);
    config.syntax_highlighting = JsonBool(
        root, "syntax_highlighting", config.syntax_highlighting);
    config.auto_pairing = JsonBool(root, "auto_pairing", config.auto_pairing);
    config.auto_indent = JsonBool(root, "auto_indent", config.auto_indent);
    config.search_match_case = JsonBool(root, "search_match_case", config.search_match_case);
    config.search_whole_word = JsonBool(root, "search_whole_word", config.search_whole_word);
    config.tab_size = JsonInt(root, "tab_size", config.tab_size);
    if (config.tab_size != 2 && config.tab_size != 4) {
        config.tab_size = 4;
    }
    config.active_theme_name = JsonString(
        root, "active_theme_name", config.active_theme_name);

    std::vector<FavoriteEntry> favorite_entries = ExtractFavoriteEntries(root, "favorites_");
    if (favorite_entries.empty()) {
        favorite_entries = ExtractFavoriteEntries(root, "favorites");
    }
    if (favorite_entries.empty()) {
        std::vector<std::string> favorites = ExtractStringArray(root, "favorites_");
        if (favorites.empty()) {
            favorites = ExtractStringArray(root, "favorites");
        }
        for (const std::string& path : favorites) {
            const std::string normalized_path = EditorConfig::NormalizeFavoritePath(path);
            if (!normalized_path.empty() && config.FindFavorite(normalized_path) == nullptr) {
                config.favorites_.push_back({normalized_path, 0, 0});
            }
        }
    } else {
        for (const FavoriteEntry& favorite : favorite_entries) {
            if (config.FindFavorite(favorite.path) == nullptr) {
                config.favorites_.push_back(favorite);
            }
        }
    }

    for (const std::string& path : ExtractStringArray(root, "file_modal_directories")) {
        const std::string normalized_path = EditorConfig::NormalizeDirectoryPath(path);
        if (!normalized_path.empty() && !config.IsFileModalDirectory(normalized_path)) {
            config.file_modal_directories_.push_back(normalized_path);
        }
    }
    return config;
}

void ConfigManager::Save(const EditorConfig& config) const {
    EditorConfig writable_config = config;
    writable_config.SetConfigPath(path_);
    writable_config.Persist();
}

} // namespace textlt
