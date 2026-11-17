#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace textlt::actions {

constexpr std::size_t kActionMetadataCharacterLimit = 30;

std::string LimitUtf8Characters(
    const std::string& value, std::size_t max_characters);
inline std::string LimitActionMetadata(const std::string& value) {
    return LimitUtf8Characters(value, kActionMetadataCharacterLimit);
}

struct ActionItem {
    std::string id;
    std::string title;
    std::string description;
    std::string content;
    bool hide_text = false;
};

struct ActionPage {
    std::string id;
    std::string name;
    std::vector<ActionItem> items;
};

struct ActionGroup {
    std::string id;
    std::string name;
    std::optional<std::string> project_id;
    bool auto_named = false;
    std::vector<ActionPage> pages;
    bool trash = false;
};

struct ActionsDocument {
    int schema_version = 1;
    std::vector<ActionGroup> groups;
};

std::string GenerateActionId();

} // namespace textlt::actions
