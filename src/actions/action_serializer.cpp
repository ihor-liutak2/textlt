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
    ActionsDocument& document, std::string& error) {
    error.clear();
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
            json.value("schema_version", 0) != 1 || !json.contains("groups") ||
            !json["groups"].is_array()) {
            error = "Unsupported actions format or schema version.";
            return false;
        }
        ActionsDocument parsed;
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
                    ActionItem item;
                    if (!item_json.is_object() || !ReadString(item_json, "id", item.id) ||
                        !ReadString(item_json, "title", item.title) ||
                        !ReadString(item_json, "description", item.description) ||
                        !ReadString(item_json, "content", item.content) || !AddId(item.id, ids)) {
                        error = "Invalid action item."; return false;
                    }
                    item.hide_text = item_json.value("hide_text", false);
                    page.items.push_back(std::move(item));
                }
                group.pages.push_back(std::move(page));
            }
            parsed.groups.push_back(std::move(group));
        }
        document = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Invalid actions JSON: ") + exception.what();
        return false;
    }
}

bool ActionSerializer::Save(const std::filesystem::path& path,
    const ActionsDocument& document, std::string& error) {
    Json json = {{"format", "textlt-actions"}, {"schema_version", 1},
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
                page_json["items"].push_back({{"id", item.id}, {"title", item.title},
                    {"description", item.description}, {"content", item.content},
                    {"hide_text", item.hide_text}});
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
