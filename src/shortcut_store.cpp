#include "shortcut_store.hpp"

#include <filesystem>
#include <map>
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

    // Version 2 writes every effective shortcut. Older files only contain the
    // overrides, so fall back to them when the full snapshot is absent.
    const auto shortcuts_it = root.find("shortcuts");
    const auto overrides_it = root.find("overrides");
    const auto* source =
        shortcuts_it != root.end() && shortcuts_it->is_object() && !shortcuts_it->empty()
            ? &*shortcuts_it
            : (overrides_it != root.end() && overrides_it->is_object() ? &*overrides_it : nullptr);
    if (!source) {
        return result;
    }
    for (auto it = source->begin(); it != source->end(); ++it) {
        if (it.value().is_string()) {
            result.emplace(it.key(), it.value().get<std::string>());
        }
    }
    return result;
}

bool ShortcutStore::Save(
    const std::unordered_map<std::string, std::string>& overrides,
    std::string* error,
    const std::unordered_map<std::string, std::string>* all_shortcuts) const {
    Json root = Json::object();
    root["version"] = 2;
    if (all_shortcuts) {
        root["shortcuts"] = Json::object();
        const std::map<std::string, std::string> sorted(
            all_shortcuts->begin(), all_shortcuts->end());
        for (const auto& item : sorted) {
            root["shortcuts"][item.first] = item.second;
        }
    } else {
        root["overrides"] = Json::object();
        const std::map<std::string, std::string> sorted(
            overrides.begin(), overrides.end());
        for (const auto& item : sorted) {
            root["overrides"][item.first] = item.second;
        }
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
