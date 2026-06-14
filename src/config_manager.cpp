#include "config_manager.hpp"

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

} // namespace

ConfigManager::ConfigManager(std::filesystem::path path)
    : path_(std::move(path)) {}

EditorConfig ConfigManager::Load() const {
    EditorConfig config;
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
    return config;
}

void ConfigManager::Save(const EditorConfig& config) const {
    std::ofstream file(path_);
    if (!file) {
        return;
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
    file << "  \"active_theme_name\": \"" << config.active_theme_name << "\"\n";
    file << "}\n";
}

} // namespace textlt
