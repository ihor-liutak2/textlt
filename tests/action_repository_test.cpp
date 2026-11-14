#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "actions/action_repository.hpp"

using namespace textlt::actions;

static void Check(bool condition) { if (!condition) std::abort(); }

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("textlt-actions-" + GenerateActionId());
    const auto path = root / "actions.json";
    ActionRepository repository(path);
    std::string error, group, page, first, second;
    Check(repository.Load(error));
    Check(repository.CreateGroup("Development commands with an unrestricted name", group, error));
    Check(repository.CreatePage(group, "A page name that may wrap naturally", page, error));
    Check(repository.CreateItem(group, page, {"", "Unicode", "description", "echo Привіт\r\n第二行"}, first, error));
    Check(repository.CreateItem(group, page, {"", "Second", "", "two\nlines"}, second, error));
    Check(repository.MoveItem(group, page, second, -1, error));
    Check(repository.Data().groups.front().pages.front().items.front().id == second);
    ActionItem edited = repository.Data().groups.front().pages.front().items.front();
    edited.title = "Edited"; Check(repository.UpdateItem(group, page, edited, error));

    ActionRepository reloaded(path); Check(reloaded.Load(error));
    const auto& loaded = reloaded.Data().groups.front().pages.front();
    Check(loaded.items.front().title == "Edited");
    Check(loaded.items.back().content == "echo Привіт\r\n第二行");

    Check(reloaded.AppendItems(group, page, {
        {"", "database.host", "", "localhost"},
        {"", "database.port", "", "5432"},
    }, error));
    const auto& imported = reloaded.Data().groups.front().pages.front().items;
    Check(imported.size() == 4);
    Check(imported[2].title == "database.host" && imported[2].content == "localhost");
    Check(imported[3].title == "database.port" && imported[3].content == "5432");

    const ActionsDocument before = reloaded.Data();
    ActionRepository failing(path, [](const auto&, const auto&, std::string& message) {
        message = "injected failure"; return false;
    });
    Check(failing.Load(error));
    std::string unused;
    Check(!failing.CreateGroup("Must not commit", unused, error));
    Check(failing.Data().groups.size() == before.groups.size());
    ActionRepository still_valid(path); Check(still_valid.Load(error));
    Check(still_valid.Data().groups.size() == before.groups.size());

    // Soft delete: the page moves to the "Deleted" group; deleting from there
    // is permanent.
    std::string soft_page;
    Check(still_valid.CreatePage(group, "To be deleted", soft_page, error));
    Check(still_valid.SoftDeletePage(group, soft_page, error));
    const ActionGroup* trash_group = nullptr;
    for (const auto& entry : still_valid.Data().groups) if (entry.trash) trash_group = &entry;
    Check(trash_group != nullptr && trash_group->name == "Deleted");
    Check(trash_group->pages.size() == 1 && trash_group->pages.front().id == soft_page);
    Check(still_valid.Data().groups.size() == before.groups.size() + 1);

    ActionRepository trashed(path); Check(trashed.Load(error));
    const ActionGroup* loaded_trash = nullptr;
    for (const auto& entry : trashed.Data().groups) if (entry.trash) loaded_trash = &entry;
    Check(loaded_trash != nullptr && loaded_trash->pages.size() == 1);
    Check(trashed.SoftDeletePage(loaded_trash->id, soft_page, error));
    for (const auto& entry : trashed.Data().groups)
        for (const auto& page_entry : entry.pages) Check(page_entry.id != soft_page);

    // hide_text survives serialization.
    std::string hidden;
    Check(trashed.CreateItem(group, page, {"", "Secret", "", "token", true}, hidden, error));
    ActionRepository verify(path); Check(verify.Load(error));
    bool hidden_ok = false;
    for (const auto& entry : verify.Data().groups)
        for (const auto& page_entry : entry.pages)
            for (const auto& item_entry : page_entry.items)
                if (item_entry.id == hidden)
                    hidden_ok = item_entry.hide_text && item_entry.content == "token";
    Check(hidden_ok);

    std::filesystem::remove_all(root);
}
