#include "actions/action_repository.hpp"

#include <algorithm>
#include <cstdlib>

#include "actions/action_serializer.hpp"

namespace textlt::actions {
namespace {

template <typename T>
bool MoveById(std::vector<T>& values, const std::string& id, int delta) {
    const auto found = std::find_if(values.begin(), values.end(),
        [&](const T& value) { return value.id == id; });
    if (found == values.end()) return false;
    const auto index = static_cast<size_t>(std::distance(values.begin(), found));
    if ((delta < 0 && index == 0) || (delta > 0 && index + 1 >= values.size()) || delta == 0)
        return true;
    std::iter_swap(values.begin() + index, values.begin() + index + (delta < 0 ? -1 : 1));
    return true;
}

} // namespace

ActionRepository::ActionRepository(std::filesystem::path path, Writer writer)
    : path_(path.empty() ? DefaultPath() : std::move(path)),
      writer_(writer ? std::move(writer) : ActionSerializer::Save) {}

std::filesystem::path ActionRepository::DefaultPath() {
#ifdef _WIN32
    if (const char* value = std::getenv("LOCALAPPDATA")) return std::filesystem::path(value) / "textlt" / "actions.json";
#elif defined(__APPLE__)
    if (const char* value = std::getenv("HOME")) return std::filesystem::path(value) / "Library" / "Application Support" / "textlt" / "actions.json";
#else
    if (const char* value = std::getenv("XDG_DATA_HOME")) return std::filesystem::path(value) / "textlt" / "actions.json";
    if (const char* value = std::getenv("HOME")) return std::filesystem::path(value) / ".local" / "share" / "textlt" / "actions.json";
#endif
    return std::filesystem::current_path() / ".textlt" / "actions.json";
}

bool ActionRepository::Load(std::string& error) {
    ActionsDocument candidate;
    if (!ActionSerializer::Load(path_, candidate, error)) return false;
    data_ = std::move(candidate);
    return true;
}

bool ActionRepository::Save(std::string& error) const { return writer_(path_, data_, error); }

bool ActionRepository::Commit(ActionsDocument candidate, std::string& error) {
    if (!writer_(path_, candidate, error)) return false;
    data_ = std::move(candidate);
    return true;
}

ActionGroup* ActionRepository::FindGroup(ActionsDocument& data, const std::string& id) {
    const auto found = std::find_if(data.groups.begin(), data.groups.end(),
        [&](const ActionGroup& group) { return group.id == id; });
    return found == data.groups.end() ? nullptr : &*found;
}

ActionPage* ActionRepository::FindPage(ActionGroup& group, const std::string& id) {
    const auto found = std::find_if(group.pages.begin(), group.pages.end(),
        [&](const ActionPage& page) { return page.id == id; });
    return found == group.pages.end() ? nullptr : &*found;
}

bool ActionRepository::CreateGroup(const std::string& name, std::string& id, std::string& error) {
    if (name.empty()) { error = "Group name is required."; return false; }
    ActionsDocument next = data_; id = GenerateActionId();
    next.groups.push_back({id, name, std::nullopt, false, {}});
    return Commit(std::move(next), error);
}

bool ActionRepository::RenameGroup(const std::string& id, const std::string& name, std::string& error) {
    if (name.empty()) { error = "Group name is required."; return false; }
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, id);
    if (!group) { error = "Group was not found."; return false; }
    if (group->project_id) { error = "Project groups cannot be renamed."; return false; }
    group->name = name; return Commit(std::move(next), error);
}

bool ActionRepository::DeleteGroup(const std::string& id, std::string& error) {
    ActionsDocument next = data_;
    const auto found = std::find_if(next.groups.begin(), next.groups.end(), [&](const auto& g) { return g.id == id; });
    if (found == next.groups.end()) { error = "Group was not found."; return false; }
    if (found->project_id) { error = "Project groups cannot be deleted."; return false; }
    next.groups.erase(found); return Commit(std::move(next), error);
}

bool ActionRepository::MoveGroup(const std::string& id, int delta, std::string& error) {
    ActionsDocument next = data_; if (!MoveById(next.groups, id, delta)) { error = "Group was not found."; return false; }
    return Commit(std::move(next), error);
}

bool ActionRepository::CreatePage(const std::string& group_id, const std::string& name, std::string& id, std::string& error) {
    if (name.empty()) { error = "Page name is required."; return false; }
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id);
    if (!group) { error = "Group was not found."; return false; }
    id = GenerateActionId(); group->pages.push_back({id, name, {}}); return Commit(std::move(next), error);
}

bool ActionRepository::RenamePage(const std::string& group_id, const std::string& id, const std::string& name, std::string& error) {
    if (name.empty()) { error = "Page name is required."; return false; }
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id); ActionPage* page = group ? FindPage(*group, id) : nullptr;
    if (!page) { error = "Page was not found."; return false; } page->name = name; return Commit(std::move(next), error);
}

bool ActionRepository::DeletePage(const std::string& group_id, const std::string& id, std::string& error) {
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id);
    if (!group) { error = "Group was not found."; return false; }
    const auto found = std::find_if(group->pages.begin(), group->pages.end(), [&](const auto& p) { return p.id == id; });
    if (found == group->pages.end()) { error = "Page was not found."; return false; }
    group->pages.erase(found); return Commit(std::move(next), error);
}

bool ActionRepository::SoftDeletePage(const std::string& group_id, const std::string& id, std::string& error) {
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id);
    if (!group) { error = "Group was not found."; return false; }
    const auto found = std::find_if(group->pages.begin(), group->pages.end(), [&](const auto& p) { return p.id == id; });
    if (found == group->pages.end()) { error = "Page was not found."; return false; }
    if (group->trash) { group->pages.erase(found); return Commit(std::move(next), error); }
    ActionPage page = std::move(*found); group->pages.erase(found);
    ActionGroup* trash = nullptr;
    for (auto& candidate : next.groups) if (candidate.trash) { trash = &candidate; break; }
    if (!trash) {
        ActionGroup created; created.id = GenerateActionId(); created.name = "Deleted"; created.trash = true;
        next.groups.push_back(std::move(created)); trash = &next.groups.back();
    }
    trash->pages.push_back(std::move(page)); return Commit(std::move(next), error);
}

bool ActionRepository::MovePage(const std::string& group_id, const std::string& id, int delta, std::string& error) {
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id);
    if (!group || !MoveById(group->pages, id, delta)) { error = "Page was not found."; return false; }
    return Commit(std::move(next), error);
}

bool ActionRepository::CreateItem(const std::string& group_id, const std::string& page_id, ActionItem item, std::string& id, std::string& error) {
    if (item.title.empty()) { error = "Action title is required."; return false; }
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id); ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page) { error = "Page was not found."; return false; }
    id = GenerateActionId(); item.id = id; page->items.push_back(std::move(item)); return Commit(std::move(next), error);
}

bool ActionRepository::AppendItems(const std::string& group_id,
    const std::string& page_id, std::vector<ActionItem> items,
    std::string& error) {
    if (items.empty()) { error = "No actions were provided."; return false; }
    ActionsDocument next = data_;
    ActionGroup* group = FindGroup(next, group_id);
    ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page) { error = "Select an Actions page before importing."; return false; }
    for (ActionItem& item : items) {
        if (item.title.empty()) { error = "Imported action title is empty."; return false; }
        item.id = GenerateActionId();
        page->items.push_back(std::move(item));
    }
    return Commit(std::move(next), error);
}

bool ActionRepository::UpdateItem(const std::string& group_id, const std::string& page_id, const ActionItem& item, std::string& error) {
    if (item.title.empty()) { error = "Action title is required."; return false; }
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id); ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page) { error = "Page was not found."; return false; }
    const auto found = std::find_if(page->items.begin(), page->items.end(), [&](const auto& value) { return value.id == item.id; });
    if (found == page->items.end()) { error = "Action was not found."; return false; }
    *found = item; return Commit(std::move(next), error);
}

bool ActionRepository::DeleteItem(const std::string& group_id, const std::string& page_id, const std::string& id, std::string& error) {
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id); ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page) { error = "Page was not found."; return false; }
    const auto found = std::find_if(page->items.begin(), page->items.end(), [&](const auto& value) { return value.id == id; });
    if (found == page->items.end()) { error = "Action was not found."; return false; }
    page->items.erase(found); return Commit(std::move(next), error);
}

bool ActionRepository::MoveItem(const std::string& group_id, const std::string& page_id, const std::string& id, int delta, std::string& error) {
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id); ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page || !MoveById(page->items, id, delta)) { error = "Action was not found."; return false; }
    return Commit(std::move(next), error);
}

bool ActionRepository::EnsureProjectGroup(const std::string& project_id, const std::string& project_name, std::string& group_id, std::string& error) {
    if (project_id.empty()) { error = "Project is not available."; return false; }
    for (const auto& group : data_.groups) if (group.project_id && *group.project_id == project_id) { group_id = group.id; return true; }
    ActionsDocument next = data_; group_id = GenerateActionId();
    std::string name = project_name.empty() ? "Project" : project_name;
    next.groups.insert(next.groups.begin(), {group_id, name, project_id, true, {}});
    return Commit(std::move(next), error);
}

} // namespace textlt::actions
