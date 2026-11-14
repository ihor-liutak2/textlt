#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "actions/action_model.hpp"

namespace textlt::actions {

class ActionRepository {
public:
    using Writer = std::function<bool(const std::filesystem::path&,
        const ActionsDocument&, std::string&)>;

    explicit ActionRepository(std::filesystem::path path = {}, Writer writer = {});
    bool Load(std::string& error);
    bool Save(std::string& error) const;
    const ActionsDocument& Data() const { return data_; }

    bool CreateGroup(const std::string& name, std::string& id, std::string& error);
    bool RenameGroup(const std::string& id, const std::string& name, std::string& error);
    bool DeleteGroup(const std::string& id, std::string& error);
    bool MoveGroup(const std::string& id, int delta, std::string& error);
    bool CreatePage(const std::string& group_id, const std::string& name,
        std::string& id, std::string& error);
    bool RenamePage(const std::string& group_id, const std::string& id,
        const std::string& name, std::string& error);
    bool DeletePage(const std::string& group_id, const std::string& id,
        std::string& error);
    // Soft delete: moves the page to the trash group ("Deleted"). Pages that
    // already live in the trash group are deleted permanently.
    bool SoftDeletePage(const std::string& group_id, const std::string& id,
        std::string& error);
    bool MovePage(const std::string& group_id, const std::string& id,
        int delta, std::string& error);
    bool CreateItem(const std::string& group_id, const std::string& page_id,
        ActionItem item, std::string& id, std::string& error);
    bool AppendItems(const std::string& group_id, const std::string& page_id,
        std::vector<ActionItem> items, std::string& error);
    bool UpdateItem(const std::string& group_id, const std::string& page_id,
        const ActionItem& item, std::string& error);
    bool DeleteItem(const std::string& group_id, const std::string& page_id,
        const std::string& id, std::string& error);
    bool MoveItem(const std::string& group_id, const std::string& page_id,
        const std::string& id, int delta, std::string& error);
    bool EnsureProjectGroup(const std::string& project_id,
        const std::string& project_name, std::string& group_id, std::string& error);

    static std::filesystem::path DefaultPath();

private:
    bool Commit(ActionsDocument candidate, std::string& error);
    static ActionGroup* FindGroup(ActionsDocument& data, const std::string& id);
    static ActionPage* FindPage(ActionGroup& group, const std::string& id);
    std::filesystem::path path_;
    Writer writer_;
    ActionsDocument data_;
};

} // namespace textlt::actions
