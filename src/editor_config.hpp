#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "distraction_mode_controller.hpp"

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
    std::string tts_audio_player_id = "auto";
    std::string tts_audio_player_command;
    std::string tts_player_voice_id;
    std::string ai_server_url = "http://127.0.0.1:11434";
    std::string ai_provider = "ollama";
    std::string ai_selected_model_key;
    std::string ai_translation_source_language = "English";
    std::string ai_translation_language = "Ukrainian";
    std::string ai_edit_style = "conversational";
    bool ai_whole_document = false;
    DistractionModeSettings distraction_mode;
    std::vector<std::string> file_modal_directories_;

    bool SetActiveThemeName(const std::string& name);

    bool AddFileModalDirectory(const std::string& path);
    bool RemoveFileModalDirectory(const std::string& path);
    bool IsFileModalDirectory(const std::string& path) const;

    void SetConfigPath(std::filesystem::path path);
    bool Persist() const;

    static std::filesystem::path DefaultConfigPath();
    static std::filesystem::path FallbackConfigPath();
    static std::string NormalizeDirectoryPath(const std::string& path);

private:
    std::filesystem::path config_path_;
};

} // namespace textlt
