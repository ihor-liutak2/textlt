#include "opened_config.hpp"

#include <cstdlib>

#include "editor_config.hpp"
#include "json_utils.hpp"

namespace textlt {
namespace {

std::filesystem::path DefaultOpenedConfigPath() {
#ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && !std::string(app_data).empty()) {
        return std::filesystem::path(app_data) / "textlt" / "opened_config.json";
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile) / "AppData" / "Roaming" /
               "textlt" / "opened_config.json";
    }
    return "opened_config.json";
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return "opened_config.json";
    }
    return std::filesystem::path(home) / ".config" / "textlt" / "opened_config.json";
#endif
}

} // namespace

OpenedConfigStore::OpenedConfigStore()
    : path_(DefaultOpenedConfigPath()) {}

OpenedConfig OpenedConfigStore::Load() const {
    OpenedConfig config;
    const Json root = LoadJsonObject(path_);
    config.active_index = JsonSize(root, "active_index", 0);

    const auto files_iter = root.find("opened_files");
    if (files_iter != root.end() && files_iter->is_array()) {
        for (const Json& object : *files_iter) {
            if (!object.is_object()) {
                continue;
            }
            OpenedFileState entry;
            entry.memory_only = JsonBool(object, "memory_only", false);
            entry.path = JsonString(object, "path");
            entry.content = JsonString(object, "content");
            entry.row = JsonSize(object, "row", 0);
            entry.column = JsonSize(object, "column", 0);

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
    }

    if (config.active_index >= config.files.size()) {
        config.active_index = config.files.empty() ? 0 : config.files.size() - 1;
    }
    return config;
}

bool OpenedConfigStore::Save(const OpenedConfig& config) const {
    Json root = Json::object();
    root["active_index"] = config.active_index;
    root["opened_files"] = Json::array();
    for (const OpenedFileState& entry : config.files) {
        Json file = {
            {"memory_only", entry.memory_only},
            {"path", entry.path.string()},
            {"row", entry.row},
            {"column", entry.column},
        };
        if (entry.memory_only) {
            file["content"] = entry.content;
        }
        root["opened_files"].push_back(std::move(file));
    }
    return WriteJsonAtomically(path_, root);
}

std::filesystem::path OpenedConfigStore::Path() const {
    return path_;
}

} // namespace textlt
