#include "opened_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#include "editor_config.hpp"

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

std::string JsonUnescape(const std::string& value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    bool escaping = false;
    for (char character : value) {
        if (escaping) {
            switch (character) {
                case 'n': unescaped.push_back('\n'); break;
                case 'r': unescaped.push_back('\r'); break;
                case 't': unescaped.push_back('\t'); break;
                case 'b': unescaped.push_back('\b'); break;
                case 'f': unescaped.push_back('\f'); break;
                default: unescaped.push_back(character); break;
            }
            escaping = false;
            continue;
        }
        if (character == '\\') {
            escaping = true;
            continue;
        }
        unescaped.push_back(character);
    }
    if (escaping) {
        unescaped.push_back('\\');
    }
    return unescaped;
}

std::filesystem::path DefaultOpenedConfigPath() {
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return "opened_config.json";
    }
    return std::filesystem::path(home) / ".config" / "textlt" / "opened_config.json";
}

size_t ExtractSize(const std::string& content, const std::string& key, size_t fallback) {
    const std::string token = "\"" + key + "\"";
    const size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    const size_t colon = content.find(':', key_pos + token.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    const size_t value_pos = content.find_first_not_of(" \t\n\r", colon + 1);
    if (value_pos == std::string::npos) {
        return fallback;
    }
    size_t end = value_pos;
    while (end < content.size() && content[end] >= '0' && content[end] <= '9') {
        ++end;
    }
    if (end == value_pos) {
        return fallback;
    }
    try {
        return static_cast<size_t>(std::stoull(content.substr(value_pos, end - value_pos)));
    } catch (...) {
        return fallback;
    }
}

bool ExtractBool(const std::string& content, const std::string& key, bool fallback) {
    const std::string token = "\"" + key + "\"";
    const size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return fallback;
    }
    const size_t colon = content.find(':', key_pos + token.size());
    if (colon == std::string::npos) {
        return fallback;
    }
    const size_t value_pos = content.find_first_not_of(" \t\n\r", colon + 1);
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

std::string ExtractString(const std::string& content, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    const size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return "";
    }
    const size_t colon = content.find(':', key_pos + token.size());
    if (colon == std::string::npos) {
        return "";
    }
    const size_t first_quote = content.find('"', colon + 1);
    if (first_quote == std::string::npos) {
        return "";
    }

    bool escaping = false;
    for (size_t index = first_quote + 1; index < content.size(); ++index) {
        if (escaping) {
            escaping = false;
            continue;
        }
        if (content[index] == '\\') {
            escaping = true;
            continue;
        }
        if (content[index] == '"') {
            return JsonUnescape(content.substr(first_quote + 1, index - first_quote - 1));
        }
    }
    return "";
}

std::vector<std::string> ExtractObjects(const std::string& content, const std::string& key) {
    std::vector<std::string> objects;
    const std::string token = "\"" + key + "\"";
    const size_t key_pos = content.find(token);
    if (key_pos == std::string::npos) {
        return objects;
    }
    const size_t colon = content.find(':', key_pos + token.size());
    const size_t array_start = content.find('[', colon);
    if (colon == std::string::npos || array_start == std::string::npos) {
        return objects;
    }

    bool in_string = false;
    bool escaping = false;
    int depth = 0;
    size_t object_start = std::string::npos;
    for (size_t index = array_start + 1; index < content.size(); ++index) {
        const char character = content[index];
        if (in_string) {
            if (escaping) {
                escaping = false;
            } else if (character == '\\') {
                escaping = true;
            } else if (character == '"') {
                in_string = false;
            }
            continue;
        }

        if (character == '"') {
            in_string = true;
            continue;
        }
        if (character == '{') {
            if (depth == 0) {
                object_start = index;
            }
            ++depth;
            continue;
        }
        if (character == '}') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && object_start != std::string::npos) {
                    objects.push_back(content.substr(object_start, index - object_start + 1));
                    object_start = std::string::npos;
                }
            }
            continue;
        }
        if (character == ']' && depth == 0) {
            break;
        }
    }
    return objects;
}

bool WriteAtomically(const std::filesystem::path& path, const OpenedConfig& config) {
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
        file << "  \"active_index\": " << config.active_index << ",\n";
        file << "  \"opened_files\": [\n";
        for (size_t index = 0; index < config.files.size(); ++index) {
            const OpenedFileState& entry = config.files[index];
            file << "    {\n";
            file << "      \"memory_only\": " << (entry.memory_only ? "true" : "false") << ",\n";
            file << "      \"path\": \"" << JsonEscape(entry.path.string()) << "\",\n";
            file << "      \"row\": " << entry.row << ",\n";
            file << "      \"column\": " << entry.column;
            if (entry.memory_only) {
                file << ",\n";
                file << "      \"content\": \"" << JsonEscape(entry.content) << "\"\n";
            } else {
                file << "\n";
            }
            file << "    }";
            if (index + 1 < config.files.size()) {
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

OpenedConfigStore::OpenedConfigStore()
    : path_(DefaultOpenedConfigPath()) {}

OpenedConfig OpenedConfigStore::Load() const {
    OpenedConfig config;
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        return config;
    }

    const std::string content((std::istreambuf_iterator<char>(file)), {});
    config.active_index = ExtractSize(content, "active_index", 0);

    for (const std::string& object : ExtractObjects(content, "opened_files")) {
        OpenedFileState entry;
        entry.memory_only = ExtractBool(object, "memory_only", false);
        entry.path = ExtractString(object, "path");
        entry.content = ExtractString(object, "content");
        entry.row = ExtractSize(object, "row", 0);
        entry.column = ExtractSize(object, "column", 0);

        if (entry.memory_only) {
            if (!entry.content.empty()) {
                config.files.push_back(std::move(entry));
            }
            continue;
        }

        const std::string normalized = EditorConfig::NormalizeFavoritePath(entry.path.string());
        if (normalized.empty()) {
            continue;
        }
        entry.path = normalized;
        config.files.push_back(std::move(entry));
    }

    if (config.active_index >= config.files.size()) {
        config.active_index = config.files.empty() ? 0 : config.files.size() - 1;
    }
    return config;
}

bool OpenedConfigStore::Save(const OpenedConfig& config) const {
    return WriteAtomically(path_, config);
}

std::filesystem::path OpenedConfigStore::Path() const {
    return path_;
}

} // namespace textlt
