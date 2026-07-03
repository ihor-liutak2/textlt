#include "shortcut_store.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "editor_config.hpp"
#include "json_utils.hpp"

namespace textlt {
namespace {

std::filesystem::path DefaultShortcutConfigPath() {
    const std::filesystem::path config_path = EditorConfig::DefaultConfigPath();
    const std::filesystem::path directory = config_path.parent_path();
    if (directory.empty()) {
        return std::filesystem::path("shortcuts.json");
    }
    return directory / "shortcuts.json";
}

} // namespace

ShortcutStore::ShortcutStore(std::filesystem::path path)
    : path_(path.empty() ? DefaultShortcutConfigPath() : std::move(path)) {}

std::unordered_map<std::string, std::string> ShortcutStore::Load() const {
    std::unordered_map<std::string, std::string> result;
    const Json root = LoadJsonObject(path_);
    const auto overrides_it = root.find("overrides");
    if (overrides_it == root.end() || !overrides_it->is_object()) {
        return result;
    }
    for (auto it = overrides_it->begin(); it != overrides_it->end(); ++it) {
        if (it.value().is_string()) {
            result.emplace(it.key(), it.value().get<std::string>());
        }
    }
    return result;
}

bool ShortcutStore::Save(const std::unordered_map<std::string, std::string>& overrides, std::string* error) const {
    Json root = Json::object();
    root["version"] = 1;
    root["overrides"] = Json::object();
    for (const auto& item : overrides) {
        root["overrides"][item.first] = item.second;
    }
    if (WriteJsonAtomically(path_, root)) {
        if (error) {
            error->clear();
        }
        return true;
    }
    if (error) {
        *error = "Could not write shortcut settings to " + path_.string();
    }
    return false;
}

} // namespace textlt
