#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "actions/action_repository.hpp"

using namespace textlt::actions;

static void Check(bool condition) { if (!condition) std::abort(); }

static void CheckMerge(ActionRepository& repo, const std::string& source, const std::string& target,
    bool expect_ok, std::size_t expect_group_count, const std::string& expect_target_name,
    std::vector<std::string> expect_page_names, std::string& error) {
    const bool ok = repo.MergeGroups(source, target, error);
    Check(ok == expect_ok);
    const auto& groups = repo.Data().groups;
    Check(groups.size() == expect_group_count);
    if (expect_group_count == 1) {
        Check(groups.front().id == target);
        Check(groups.front().name == expect_target_name);
        std::vector<std::string> names;
        for (const auto& page : groups.front().pages) names.push_back(page.name);
        Check(names == expect_page_names);
    }
}

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

    // Find/Replace applies grouped text edits in one repository commit, with
    // descending offsets preserving positions for repeated matches.
    const auto revision_before_replace = reloaded.Revision();
    Check(reloaded.ApplyTextEdits(group, page, {
        {first, 2, 5, std::string("Привіт").size(), "Hello"},
        {first, 2, 0, 4, "run"},
        {first, 0, 0, std::string("Unicode").size(), "Localized"},
    }, error));
    Check(reloaded.Revision() == revision_before_replace + 1);
    const auto& replaced = reloaded.Data().groups.front().pages.front().items;
    const auto replaced_first = std::find_if(replaced.begin(), replaced.end(),
        [&](const ActionItem& item) { return item.id == first; });
    Check(replaced_first != replaced.end());
    Check(replaced_first->title == "Localized");
    Check(replaced_first->content == "run Hello\r\n第二行");

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

    // Merge: source pages move into target; clashing page names are renumbered
    // with " (1)", " (2)", ...; the source group is removed.
    {
        const auto merge_root =
            std::filesystem::temp_directory_path() / ("textlt-actions-merge-" + GenerateActionId());
        const auto merge_path = merge_root / "actions.json";
        ActionRepository repo(merge_path);
        Check(repo.Load(error));
        std::string g1, g2, p1, p2, p3, unused;
        Check(repo.CreateGroup("Deploy", g1, error));
        Check(repo.CreatePage(g1, "Main", p1, error));
        Check(repo.CreateItem(g1, p1, {"", "a", "", "one"}, unused, error));
        Check(repo.CreateGroup("Deploy (1)", g2, error));
        Check(repo.CreatePage(g2, "Main", p2, error));
        Check(repo.CreatePage(g2, "Extra", p3, error));
        Check(repo.CreateItem(g2, p2, {"", "b", "", "two"}, unused, error));
        Check(repo.CreateItem(g2, p3, {"", "c", "", "three"}, unused, error));

        CheckMerge(repo, g1, g2, true, 1, "Deploy (1)", {"Main", "Extra", "Main (1)"}, error);

        const auto& merged = repo.Data().groups.front();
        bool main_has_b = false, main1_has_a = false;
        for (const auto& pg : merged.pages) {
            for (const auto& item : pg.items) {
                if (pg.name == "Main" && item.content == "two") main_has_b = true;
                if (pg.name == "Main (1)" && item.content == "one") main1_has_a = true;
            }
        }
        Check(main_has_b && main1_has_a);

        // Merging into itself fails.
        Check(!repo.MergeGroups(g2, g2, error));
        std::filesystem::remove_all(merge_root);
    }

    std::filesystem::remove_all(root);
}
