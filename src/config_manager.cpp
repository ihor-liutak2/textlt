#include "config_manager.hpp"

#include <algorithm>
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
    config.show_notes_sidebar = JsonBool(
        root, "show_notes_sidebar", config.show_notes_sidebar);
    config.show_actions_sidebar = JsonBool(
        root, "show_actions_sidebar", config.show_actions_sidebar);
    config.notes_editor_columns = JsonInt(
        root, "notes_editor_columns", config.notes_editor_columns);
    config.notes_editor_columns = std::clamp(config.notes_editor_columns, 1, 3);
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
    const auto theme_overrides = root.find("theme_color_overrides");
    if (theme_overrides != root.end() && theme_overrides->is_object()) {
        for (auto theme = theme_overrides->begin(); theme != theme_overrides->end(); ++theme) {
            if (!theme.value().is_object()) {
                continue;
            }
            for (auto color = theme.value().begin(); color != theme.value().end(); ++color) {
                if (color.value().is_string()) {
                    config.theme_color_overrides[theme.key()][color.key()] =
                        color.value().get<std::string>();
                }
            }
        }
    }
    config.tts_audio_player_id = JsonString(
        root, "tts_audio_player_id", config.tts_audio_player_id);
    if (config.tts_audio_player_id.empty()) {
        config.tts_audio_player_id = "auto";
    }
    config.tts_audio_player_command = JsonString(
        root, "tts_audio_player_command", config.tts_audio_player_command);
    config.tts_player_voice_id = JsonString(
        root, "tts_player_voice_id", config.tts_player_voice_id);
    const auto providers = root.find("ai_providers");
    if (providers != root.end() && providers->is_object()) {
        config.ai_active_provider = JsonString(root, "ai_active_provider");
        for (auto it = providers->begin(); it != providers->end(); ++it) {
            if (!it->is_object()) {
                continue;
            }
            auto& provider = config.ai_providers[it.key()];
            provider.endpoint = JsonString(*it, "endpoint");
            provider.api_key = JsonString(*it, "api_key");
            provider.model = JsonString(*it, "model");
            if (it.key() == "opencode") {
                provider.opencode_use_go = JsonBool(*it, "use_go",
                    provider.endpoint != "https://opencode.ai/inference");
            }
            const auto efforts = it->find("model_efforts");
            if (efforts != it->end() && efforts->is_object()) {
                for (auto effort = efforts->begin(); effort != efforts->end(); ++effort) {
                    if (effort->is_string()) {
                        provider.model_efforts[effort.key()] = effort->get<std::string>();
                    }
                }
            }
        }
    } else {
        const std::string old_provider = JsonString(root, "ai_provider");
        const std::string key = JsonString(root, "ai_selected_model_key");
        config.ai_active_provider =
            key.rfind("openai:", 0) == 0 || old_provider == "openai" ||
            old_provider == "llama_cpp" ? "openai" : "ollama";
        if (key.rfind("ollama:", 0) == 0) {
            config.ai_active_provider = "ollama";
        }
        auto& provider = config.ai_providers[config.ai_active_provider];
        provider.endpoint = JsonString(root, "ai_server_url");
        const std::string prefix = config.ai_active_provider + ":";
        if (key.rfind(prefix, 0) == 0) {
            provider.model = key.substr(prefix.size());
        } else if (key.rfind("local:", 0) != 0) {
            provider.model = key;
        }
        if (provider.model.empty()) {
            config.ai_active_provider.clear();
        }
    }
    config.distraction_mode.enabled = JsonBool(
        root, "distraction_enabled", config.distraction_mode.enabled);
    config.distraction_mode.column_count = JsonInt(
        root, "distraction_column_count", config.distraction_mode.column_count);
    config.distraction_mode.column_width = JsonInt(
        root, "distraction_column_width", config.distraction_mode.column_width);
    config.distraction_mode.column_gap = JsonInt(
        root, "distraction_column_gap", config.distraction_mode.column_gap);
    config.distraction_mode.top_padding = JsonInt(
        root, "distraction_top_padding", config.distraction_mode.top_padding);
    config.distraction_mode.bottom_padding = JsonInt(
        root, "distraction_bottom_padding", config.distraction_mode.bottom_padding);
    config.distraction_mode = DistractionModeController::NormalizeSettings(config.distraction_mode);

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
