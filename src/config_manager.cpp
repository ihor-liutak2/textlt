#include "config_manager.hpp"

#include <filesystem>
#include <string>
#include <vector>

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
    config.tts_audio_player_id = JsonString(
        root, "tts_audio_player_id", config.tts_audio_player_id);
    if (config.tts_audio_player_id.empty()) {
        config.tts_audio_player_id = "auto";
    }
    config.tts_audio_player_command = JsonString(
        root, "tts_audio_player_command", config.tts_audio_player_command);
    config.tts_player_voice_id = JsonString(
        root, "tts_player_voice_id", config.tts_player_voice_id);

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
