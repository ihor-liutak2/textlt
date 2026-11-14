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

    std::filesystem::remove_all(root);
}
