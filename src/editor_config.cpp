#include "editor_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace textlt {
namespace {

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

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
    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    const std::filesystem::path temp_path = path.string() + ".tmp";
    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }

        file << "{\n";
        file << "  \"show_line_numbers\": " << (config.show_line_numbers ? "true" : "false") << ",\n";
        file << "  \"show_file_explorer\": " << (config.show_file_explorer ? "true" : "false") << ",\n";
        file << "  \"smart_word_wrap\": " << (config.smart_word_wrap ? "true" : "false") << ",\n";
        file << "  \"syntax_highlighting\": " << (config.syntax_highlighting ? "true" : "false") << ",\n";
        file << "  \"auto_pairing\": " << (config.auto_pairing ? "true" : "false") << ",\n";
        file << "  \"auto_indent\": " << (config.auto_indent ? "true" : "false") << ",\n";
        file << "  \"search_match_case\": " << (config.search_match_case ? "true" : "false") << ",\n";
        file << "  \"search_whole_word\": " << (config.search_whole_word ? "true" : "false") << ",\n";
        file << "  \"tab_size\": " << config.tab_size << ",\n";
        file << "  \"active_theme_name\": \"" << JsonEscape(config.active_theme_name) << "\",\n";
        file << "  \"favorites_\": [\n";
        for (size_t index = 0; index < config.favorites_.size(); ++index) {
            const FavoriteEntry& favorite = config.favorites_[index];
            file << "    {\n";
            file << "      \"path\": \"" << JsonEscape(favorite.path) << "\",\n";
            file << "      \"row\": " << favorite.row << ",\n";
            file << "      \"column\": " << favorite.column << "\n";
            file << "    }";
            if (index + 1 < config.favorites_.size()) {
                file << ",";
            }
            file << "\n";
        }
        file << "  ]\n";
        file << "}\n";
        file.flush();
        if (!file) {
            std::filesystem::remove(temp_path, error);
            return false;
        }
    }

    std::filesystem::rename(temp_path, path, error);
    if (error) {
        std::filesystem::remove(temp_path, error);
        return false;
    }
    return true;
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
