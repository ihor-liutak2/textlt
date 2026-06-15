#include "config_manager.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace textlt {
namespace {

bool ExtractBool(const std::string& content, const std::string& key, bool fallback) {
    const std::string token = "\"" + key + "\"";
    size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    size_t colon = content.find(':', key_pos + token.size());
    size_t value_pos = content.find_first_not_of(" \t\n\r", colon + 1);
    if (value_pos == std::string::npos) {
        return fallback;
    }
    if (content.compare(value_pos, 4, "true") == 0) {
        return true;
    }
    if (content.compare(value_pos, 5, "false") == 0) {
        return false;
    }
    return fallback;
}

int ExtractInt(const std::string& content, const std::string& key, int fallback) {
    const std::string token = "\"" + key + "\"";
    size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    size_t colon = content.find(':', key_pos + token.size());
    size_t value_pos = content.find_first_not_of(" \t\n\r", colon + 1);
    if (colon == std::string::npos || value_pos == std::string::npos) {
        return fallback;
    }

    size_t end_pos = value_pos;
    while (end_pos < content.size() &&
           content[end_pos] >= '0' && content[end_pos] <= '9') {
        ++end_pos;
    }
    if (end_pos == value_pos) {
        return fallback;
    }

    try {
        return std::stoi(content.substr(value_pos, end_pos - value_pos));
    } catch (...) {
        return fallback;
    }
}

std::string ExtractString(const std::string& content, const std::string& key, std::string fallback) {
    const std::string token = "\"" + key + "\"";
    size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    size_t colon = content.find(':', key_pos + token.size());
    size_t first_quote = content.find('"', colon);
    size_t second_quote = content.find('"', first_quote + 1);
    if (colon == std::string::npos || first_quote == std::string::npos ||
        second_quote == std::string::npos) {
        return fallback;
    }
    return content.substr(first_quote + 1, second_quote - first_quote - 1);
}

std::vector<std::string> ExtractStringArray(const std::string& content, const std::string& key) {
    std::vector<std::string> values;
    const std::string token = "\"" + key + "\"";
    size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return values;
    }

    size_t colon = content.find(':', key_pos + token.size());
    size_t array_start = content.find('[', colon);
    size_t array_end = content.find(']', array_start);
    if (colon == std::string::npos ||
        array_start == std::string::npos ||
        array_end == std::string::npos) {
        return values;
    }

    bool in_string = false;
    bool escaping = false;
    std::string value;
    for (size_t index = array_start + 1; index < array_end; ++index) {
        const char character = content[index];
        if (!in_string) {
            if (character == '"') {
                in_string = true;
                value.clear();
            }
            continue;
        }

        if (escaping) {
            switch (character) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                default: value.push_back(character); break;
            }
            escaping = false;
            continue;
        }

        if (character == '\\') {
            escaping = true;
            continue;
        }
        if (character == '"') {
            in_string = false;
            values.push_back(value);
            continue;
        }
        value.push_back(character);
    }

    return values;
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
    std::ifstream file(path_);
    if (!file) {
        return config;
    }

    std::string content((std::istreambuf_iterator<char>(file)), {});
    config.show_line_numbers = ExtractBool(content, "show_line_numbers", config.show_line_numbers);
    config.show_file_explorer = ExtractBool(
        content, "show_file_explorer", config.show_file_explorer);
    config.smart_word_wrap = ExtractBool(content, "smart_word_wrap", config.smart_word_wrap);
    config.syntax_highlighting = ExtractBool(
        content, "syntax_highlighting", config.syntax_highlighting);
    config.auto_pairing = ExtractBool(content, "auto_pairing", config.auto_pairing);
    config.auto_indent = ExtractBool(content, "auto_indent", config.auto_indent);
    config.search_match_case = ExtractBool(
        content, "search_match_case", config.search_match_case);
    config.search_whole_word = ExtractBool(
        content, "search_whole_word", config.search_whole_word);
    config.tab_size = ExtractInt(content, "tab_size", config.tab_size);
    if (config.tab_size != 2 && config.tab_size != 4) {
        config.tab_size = 4;
    }
    config.active_theme_name = ExtractString(
        content, "active_theme_name", config.active_theme_name);

    std::vector<std::string> favorites = ExtractStringArray(content, "favorites_");
    if (favorites.empty()) {
        favorites = ExtractStringArray(content, "favorites");
    }
    for (const std::string& path : favorites) {
        const std::string normalized_path = EditorConfig::NormalizeFavoritePath(path);
        if (!normalized_path.empty() &&
            std::find(config.favorites_.begin(), config.favorites_.end(), normalized_path) ==
                config.favorites_.end()) {
            config.favorites_.push_back(normalized_path);
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
