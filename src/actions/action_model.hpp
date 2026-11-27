#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace textlt::actions {

constexpr std::size_t kActionMetadataCharacterLimit = 30;
constexpr std::size_t kActionCaptionCharacterLimit = 12;
constexpr std::size_t kMaxActionTabs = 8;

std::string LimitUtf8Characters(
    const std::string& value, std::size_t max_characters);
inline std::string LimitActionMetadata(const std::string& value) {
    return LimitUtf8Characters(value, kActionMetadataCharacterLimit);
}
inline std::string LimitActionCaption(const std::string& value) {
    return LimitUtf8Characters(value, kActionCaptionCharacterLimit);
}

struct ActionTab {
    std::string caption;
    std::string content;
    bool hide_text = false;
};

struct ActionItem {
    std::string id;
    std::string title;
    std::vector<ActionTab> tabs;

    ActionItem() = default;

    // Convenience constructor for the common single-tab action. It keeps
    // existing call sites and tests that provide a caption/content pair
    // readable while the model supports multiple tabs.
    ActionItem(std::string id, std::string title, std::string caption,
        std::string content, bool hide_text = false)
        : id(std::move(id)), title(std::move(title)),
          tabs{{std::move(caption), std::move(content), hide_text}} {}

    static ActionItem WithTabs(std::string id, std::string title,
        std::vector<ActionTab> tabs) {
        ActionItem item;
        item.id = std::move(id);
        item.title = std::move(title);
        item.tabs = std::move(tabs);
        return item;
    }
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
    int schema_version = 2;
    std::vector<ActionGroup> groups;
};

std::string GenerateActionId();

} // namespace textlt::actions
