#include "actions/action_repository.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "actions/action_serializer.hpp"
#include "windows_environment_path.hpp"

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
    const std::filesystem::path local_app_data = WindowsEnvironmentPath(L"LOCALAPPDATA");
    if (!local_app_data.empty()) {
        return local_app_data / "textlt" / "actions.json";
    }
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
    bool migrated = false;
    warning_.clear();
    if (!ActionSerializer::Load(path_, candidate, error, &migrated)) return false;
    data_ = std::move(candidate);
    if (migrated) {
        std::filesystem::path backup_path = path_;
        backup_path += ".v1.bak";
        std::error_code fs_error;
        const bool backup_exists = std::filesystem::exists(backup_path, fs_error);
        if (fs_error) {
            warning_ = "Actions were migrated in memory, but the backup path could not be checked; the source file was not rewritten.";
        } else if (!backup_exists) {
            fs_error.clear();
            std::filesystem::copy_file(path_, backup_path,
                std::filesystem::copy_options::none, fs_error);
            if (fs_error) {
                warning_ = "Actions were migrated in memory, but the v1 backup could not be created; the source file was not rewritten.";
            }
        }
        if (warning_.empty()) {
            std::string migration_error;
            if (!writer_(path_, data_, migration_error)) {
                warning_ = "Actions were migrated in memory, but the v2 file could not be written: " + migration_error;
            }
        }
    }
    return true;
}

bool ActionRepository::Save(std::string& error) const { return writer_(path_, data_, error); }

bool ActionRepository::Commit(ActionsDocument candidate, std::string& error) {
    if (!writer_(path_, candidate, error)) return false;
    data_ = std::move(candidate);
    ++revision_;
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

namespace {

std::string BaseGroupName(const std::string& name) {
    const size_t paren = name.rfind(" (");
    if (paren == std::string::npos) return name;
    const size_t end = name.size();
    if (end > 0 && name[end - 1] == ')') {
        const std::string inner = name.substr(paren + 2, end - paren - 3);
        if (!inner.empty() &&
            std::all_of(inner.begin(), inner.end(),
                [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            return name.substr(0, paren);
        }
    }
    return name;
}

bool PageNameTaken(const ActionGroup& group, const std::string& name) {
    return std::any_of(group.pages.begin(), group.pages.end(),
        [&](const ActionPage& page) { return page.name == name; });
}

std::string UniquePageName(ActionGroup& group, const std::string& base) {
    std::string name = base;
    int suffix = 1;
    while (PageNameTaken(group, name)) {
        name = base + " (" + std::to_string(suffix) + ")";
        ++suffix;
    }
    return name;
}

std::string NextAutomaticPageName(const ActionsDocument& document) {
    std::unordered_set<std::string> names;
    for (const auto& group : document.groups) {
        for (const auto& page : group.pages) {
            std::string name = page.name;
            std::transform(name.begin(), name.end(), name.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            names.insert(std::move(name));
        }
    }
    for (size_t number = 1; ; ++number) {
        const std::string suffix = std::to_string(number);
        if (names.count("page" + suffix) == 0) return "Page" + suffix;
    }
}

} // namespace

bool ActionRepository::MergeGroups(const std::string& source_id,
    const std::string& target_id, std::string& error) {
    ActionsDocument next = data_;
    ActionGroup* source = FindGroup(next, source_id);
    ActionGroup* target = FindGroup(next, target_id);
    if (!source || !target) { error = "Group was not found."; return false; }
    if (source == target) { error = "Cannot merge a group into itself."; return false; }

    // Rebase every source page onto the target, numbering clashing names.
    for (ActionPage& page : source->pages) {
        page.name = UniquePageName(*target, page.name);
        target->pages.push_back(std::move(page));
    }
    source->pages.clear();

    const auto found = std::find_if(next.groups.begin(), next.groups.end(),
        [&](const ActionGroup& g) { return g.id == source_id; });
    if (found == next.groups.end()) { error = "Source group was not found."; return false; }
    next.groups.erase(found);
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

bool ActionRepository::CreateItem(const std::string& group_id, const std::string& page_id,
    ActionItem item, std::string& id, std::string& error, std::string* created_page_id,
    std::string_view after_item_id) {
    if (item.title.empty()) { error = "Action title is required."; return false; }
    if (item.tabs.empty()) item.tabs.push_back({});
    if (item.tabs.size() > 4) { error = "An action can have at most four tabs."; return false; }
    for (const ActionTab& tab : item.tabs) {
        if (LimitActionCaption(tab.caption).size() != tab.caption.size()) {
            error = "Action captions are limited to 12 characters.";
            return false;
        }
    }
    ActionsDocument next = data_;
    ActionGroup* group = FindGroup(next, group_id);
    ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    std::string automatic_page_id;
    if (group && page_id.empty() && created_page_id) {
        automatic_page_id = GenerateActionId();
        group->pages.push_back({automatic_page_id, NextAutomaticPageName(next), {}});
        page = &group->pages.back();
    }
    if (!page) { error = "Page was not found."; return false; }
    const std::string item_id = GenerateActionId();
    item.id = item_id;
    const auto after = std::find_if(page->items.begin(), page->items.end(),
        [&](const ActionItem& existing) { return existing.id == after_item_id; });
    page->items.insert(after == page->items.end() ? after : after + 1, std::move(item));
    if (!Commit(std::move(next), error)) return false;
    id = item_id;
    if (created_page_id) *created_page_id = std::move(automatic_page_id);
    return true;
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
        if (item.tabs.empty()) item.tabs.push_back({});
        if (item.tabs.size() > 4) { error = "An action can have at most four tabs."; return false; }
        for (const ActionTab& tab : item.tabs) {
            if (LimitActionCaption(tab.caption).size() != tab.caption.size()) {
                error = "Action captions are limited to 12 characters.";
                return false;
            }
        }
        item.id = GenerateActionId();
        page->items.push_back(std::move(item));
    }
    return Commit(std::move(next), error);
}

bool ActionRepository::UpdateItem(const std::string& group_id, const std::string& page_id, const ActionItem& item, std::string& error) {
    if (item.title.empty()) { error = "Action title is required."; return false; }
    if (item.tabs.empty() || item.tabs.size() > 4) {
        error = "An action must have one to four tabs.";
        return false;
    }
    for (const ActionTab& tab : item.tabs) {
        if (LimitActionCaption(tab.caption).size() != tab.caption.size()) {
            error = "Action captions are limited to 12 characters.";
            return false;
        }
    }
    ActionsDocument next = data_; ActionGroup* group = FindGroup(next, group_id); ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page) { error = "Page was not found."; return false; }
    const auto found = std::find_if(page->items.begin(), page->items.end(), [&](const auto& value) { return value.id == item.id; });
    if (found == page->items.end()) { error = "Action was not found."; return false; }
    *found = item; return Commit(std::move(next), error);
}

bool ActionRepository::RenameItem(const std::string& group_id, const std::string& page_id,
    const std::string& id, const std::string& title, std::string& error) {
    error.clear();
    if (title.empty() || LimitActionMetadata(title).size() != title.size()) {
        error = "Enter an action title of 1 to 30 characters.";
        return false;
    }
    ActionsDocument next = data_;
    ActionGroup* group = FindGroup(next, group_id);
    ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page) { error = "Page was not found."; return false; }
    const auto item = std::find_if(page->items.begin(), page->items.end(),
        [&](const auto& value) { return value.id == id; });
    if (item == page->items.end()) { error = "Action was not found."; return false; }
    if (item->title == title) return true;
    item->title = title;
    return Commit(std::move(next), error);
}

bool ActionRepository::ApplyTextEdits(const std::string& group_id,
    const std::string& page_id, std::vector<ActionTextEdit> edits,
    std::string& error) {
    if (edits.empty()) return true;
    ActionsDocument next = data_;
    ActionGroup* group = FindGroup(next, group_id);
    ActionPage* page = group ? FindPage(*group, page_id) : nullptr;
    if (!page) { error = "Page was not found."; return false; }

    std::sort(edits.begin(), edits.end(), [](const ActionTextEdit& left,
        const ActionTextEdit& right) {
        if (left.item_id != right.item_id) return left.item_id > right.item_id;
        if (left.field != right.field) return left.field > right.field;
        return left.byte > right.byte;
    });
    for (const ActionTextEdit& edit : edits) {
        const auto found = std::find_if(page->items.begin(), page->items.end(),
            [&](const auto& item) { return item.id == edit.item_id; });
        if (found == page->items.end()) {
            error = "Action was not found.";
            return false;
        }
        std::string* value = nullptr;
        if (edit.field == ActionField::Title) {
            value = &found->title;
        } else {
            if (edit.tab_index < 0 ||
                static_cast<size_t>(edit.tab_index) >= found->tabs.size()) {
                error = "Action tab was not found.";
                return false;
            }
            ActionTab& tab = found->tabs[static_cast<size_t>(edit.tab_index)];
            value = edit.field == ActionField::Caption
                ? &tab.caption : &tab.content;
        }
        const size_t from = std::min(edit.byte, value->size());
        const size_t to = std::min(value->size(), from + edit.length);
        value->replace(from, to - from, edit.replacement);
    }

    for (const ActionItem& item : page->items) {
        if (item.title.empty()) {
            error = "Action title cannot be empty after replacement.";
            return false;
        }
        bool invalid_caption = false;
        for (const ActionTab& tab : item.tabs) {
            if (LimitActionCaption(tab.caption).size() !=
                tab.caption.size()) invalid_caption = true;
        }
        if (LimitUtf8Characters(item.title, kActionMetadataCharacterLimit).size() !=
                item.title.size() || invalid_caption) {
            error = "Action title is limited to 30 characters and captions to 12.";
            return false;
        }
    }
    return Commit(std::move(next), error);
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

bool ActionRepository::UpdateProjectGroupName(const std::string& project_id,
    const std::string& project_name, std::string& error) {
    error.clear();
    const std::string name = project_name.empty() ? "Project" : project_name;
    const auto& groups = data_.groups;
    const auto found = std::find_if(groups.begin(), groups.end(),
        [&](const ActionGroup& group) { return group.project_id == project_id; });
    if (found == groups.end() || found->name == name) return true;

    ActionsDocument next = data_;
    next.groups[std::distance(groups.begin(), found)].name = name;
    return Commit(std::move(next), error);
}

bool ActionRepository::DetachProjectGroup(const std::string& project_id, std::string& error) {
    error.clear();
    const auto& groups = data_.groups;
    const auto found = std::find_if(groups.begin(), groups.end(),
        [&](const ActionGroup& group) { return group.project_id == project_id; });
    if (found == groups.end()) return true;
    return ConvertGroupToGeneral(found->id, error);
}

bool ActionRepository::ConvertGroupToGeneral(const std::string& group_id, std::string& error) {
    error.clear();
    ActionsDocument next = data_;
    ActionGroup* group = FindGroup(next, group_id);
    if (!group) {
        error = "Group was not found.";
        return false;
    }
    if (!group->project_id) return true;
    group->project_id.reset();
    group->auto_named = false;
    return Commit(std::move(next), error);
}

} // namespace textlt::actions
