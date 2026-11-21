#include "actions/action_serializer.hpp"

#include <fstream>
#include <unordered_set>

#include "json_utils.hpp"

namespace textlt::actions {
namespace {

bool ReadString(const Json& object, const char* key, std::string& value) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) return false;
    value = found->get<std::string>();
    return true;
}

bool AddId(const std::string& id, std::unordered_set<std::string>& ids) {
    return !id.empty() && ids.insert(id).second;
}

} // namespace

bool ActionSerializer::Load(const std::filesystem::path& path,
    ActionsDocument& document, std::string& error, bool* migrated) {
    error.clear();
    if (migrated) *migrated = false;
    if (!std::filesystem::exists(path)) {
        document = {};
        return true;
    }
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Could not open actions file."; return false; }
        Json json = Json::parse(input, nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            error = "Invalid actions JSON.";
            return false;
        }
        if (json.value("format", "") != "textlt-actions" ||
            !json.contains("groups") || !json["groups"].is_array()) {
            error = "Unsupported actions format or schema version.";
            return false;
        }

        // Version 1 files predate the version field in some installations.
        // Treat a missing version as version 1 and only accept known versions.
        int schema_version = 1;
        if (json.contains("schema_version") && !json["schema_version"].is_null()) {
            if (!json["schema_version"].is_number_integer()) {
                error = "Unsupported actions schema version.";
                return false;
            }
            schema_version = json["schema_version"].get<int>();
        }
        if (schema_version < 1 || schema_version > 2) {
            error = "Unsupported actions format or schema version.";
            return false;
        }

        ActionsDocument parsed;
        parsed.schema_version = 2;
        std::unordered_set<std::string> ids;
        for (const Json& group_json : json["groups"]) {
            if (!group_json.is_object()) { error = "Invalid actions group."; return false; }
            ActionGroup group;
            if (!ReadString(group_json, "id", group.id) ||
                !ReadString(group_json, "name", group.name) || !AddId(group.id, ids) ||
                !group_json.contains("pages") || !group_json["pages"].is_array()) {
                error = "Invalid actions group fields."; return false;
            }
            if (group_json.contains("project_id") && !group_json["project_id"].is_null()) {
                if (!group_json["project_id"].is_string()) { error = "Invalid project association."; return false; }
                group.project_id = group_json["project_id"].get<std::string>();
            }
            group.auto_named = group_json.value("auto_named", false);
            group.trash = group_json.value("trash", false);
            for (const Json& page_json : group_json["pages"]) {
                ActionPage page;
                if (!page_json.is_object() || !ReadString(page_json, "id", page.id) ||
                    !ReadString(page_json, "name", page.name) || !AddId(page.id, ids) ||
                    !page_json.contains("items") || !page_json["items"].is_array()) {
                    error = "Invalid actions page."; return false;
                }
                for (const Json& item_json : page_json["items"]) {
                    if (!item_json.is_object()) { error = "Invalid action item."; return false; }
                    ActionItem item;
                    if (!ReadString(item_json, "id", item.id) ||
                        !ReadString(item_json, "title", item.title) ||
                        !AddId(item.id, ids)) {
                        error = "Invalid action item."; return false;
                    }

                    const bool has_tabs = item_json.contains("tabs");
                    if (schema_version >= 2 && has_tabs) {
                        const Json& tabs_json = item_json["tabs"];
                        if (!tabs_json.is_array() || tabs_json.empty() ||
                            tabs_json.size() > 4) {
                            error = "Invalid action tabs."; return false;
                        }
                        for (const Json& tab_json : tabs_json) {
                            if (!tab_json.is_object()) {
                                error = "Invalid action tab."; return false;
                            }
                            ActionTab tab;
                            if (tab_json.contains("caption") &&
                                !ReadString(tab_json, "caption", tab.caption)) {
                                error = "Invalid action tab caption."; return false;
                            }
                            if (!ReadString(tab_json, "content", tab.content)) {
                                error = "Invalid action tab content."; return false;
                            }
                            tab.hide_text = tab_json.value("hide_text", false);
                            item.tabs.push_back(std::move(tab));
                        }
                    } else {
                        // Version 1 shape, or a version 2 item that still uses
                        // the legacy fields. One tab is the faithful mapping.
                        ActionTab tab;
                        std::string description;
                        if (!ReadString(item_json, "description", description) ||
                            !ReadString(item_json, "content", tab.content)) {
                            error = "Invalid action item."; return false;
                        }
                        tab.caption = std::move(description);
                        tab.hide_text = item_json.value("hide_text", false);
                        item.tabs.push_back(std::move(tab));
                    }
                    page.items.push_back(std::move(item));
                }
                group.pages.push_back(std::move(page));
            }
            parsed.groups.push_back(std::move(group));
        }
        if (migrated) *migrated = schema_version < 2;
        document = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Invalid actions JSON: ") + exception.what();
        return false;
    }
}

bool ActionSerializer::Save(const std::filesystem::path& path,
    const ActionsDocument& document, std::string& error) {
    error.clear();
    for (const ActionGroup& group : document.groups) {
        for (const ActionPage& page : group.pages) {
            for (const ActionItem& item : page.items) {
                if (item.tabs.size() > 4) {
                    error = "An action can have at most four tabs.";
                    return false;
                }
            }
        }
    }
    Json json = {{"format", "textlt-actions"}, {"schema_version", 2},
        {"groups", Json::array()}};
    for (const auto& group : document.groups) {
        Json group_json = {{"id", group.id}, {"name", group.name},
            {"project_id", group.project_id ? Json(*group.project_id) : Json(nullptr)},
            {"auto_named", group.auto_named}, {"trash", group.trash},
            {"pages", Json::array()}};
        for (const auto& page : group.pages) {
            Json page_json = {{"id", page.id}, {"name", page.name},
                {"items", Json::array()}};
            for (const auto& item : page.items) {
                Json item_json = {{"id", item.id}, {"title", item.title},
                    {"tabs", Json::array()}};
                if (item.tabs.empty()) {
                    item_json["tabs"].push_back({{"caption", ""},
                        {"content", ""}, {"hide_text", false}});
                } else {
                    for (const auto& tab : item.tabs) {
                        item_json["tabs"].push_back({{"caption", tab.caption},
                            {"content", tab.content},
                            {"hide_text", tab.hide_text}});
                    }
                }
                page_json["items"].push_back(std::move(item_json));
            }
            group_json["pages"].push_back(std::move(page_json));
        }
        json["groups"].push_back(std::move(group_json));
    }
    if (!WriteJsonAtomically(path, json)) {
        error = "Could not atomically save actions.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace textlt::actions
