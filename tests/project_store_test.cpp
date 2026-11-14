#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>

#include "project_store.hpp"
#include "path_utils.hpp"

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace textlt;

    const std::filesystem::path base =
        std::filesystem::temp_directory_path() / "textlt_project_store_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(base, cleanup_error);
    std::filesystem::create_directories(base / "one");
    std::filesystem::create_directories(base / "two");
    const std::filesystem::path unicode_directory =
        base / PathFromUtf8("\xD0\xBF\xD1\x80\xD0\xBE\xD1\x94\xD0\xBA\xD1\x82-\xF0\x9F\x8C\xBB");
    std::filesystem::create_directories(unicode_directory);
    const std::filesystem::path registry = base / "projects.json";

    std::string error;
    std::string first_id;
    std::string second_id;
    {
        ProjectStore store(registry);
        ProjectRecord* first = store.Add(base / "one", "One", error);
        Expect(first != nullptr, "Could not add first project: " + error);
        first_id = first->id;
        ProjectRecord* duplicate = store.Add(base / "one" / ".", "Duplicate", error);
        Expect(duplicate && duplicate->id == first_id, "Equivalent roots must be deduplicated.");

        ProjectRecord* second = store.Add(base / "two", "Two", error);
        Expect(second != nullptr, "Could not add second project: " + error);
        second_id = second->id;
        Expect(store.Projects().size() == 2, "Unexpected project count.");
        Expect(store.SetRemoteConnection(second_id, "remote-two", error), error);

        ProjectRecord* unicode = store.Add(unicode_directory, {}, error);
        Expect(unicode != nullptr, "Could not add Unicode project: " + error);
        Expect(unicode->name == Utf8FromPath(unicode_directory.filename()),
               "Unicode project name was not preserved as UTF-8.");
        Expect(store.SetActive(first_id, error), error);
    }

    {
        const std::filesystem::path broken_registry = base / "broken.json";
        {
            std::ofstream output(broken_registry);
            output << "{broken";
        }
        ProjectStore broken(broken_registry);
        Expect(!broken.IsUsable(), "A malformed project registry must be rejected.");
        Expect(broken.Add(base / "one", "One", error) == nullptr,
               "A malformed registry must not be overwritten by Add.");
        std::ifstream input(broken_registry);
        std::string contents;
        std::getline(input, contents);
        Expect(contents == "{broken", "Malformed registry contents must be preserved.");
    }

    {
        ProjectStore store(registry);
        Expect(store.Projects().size() == 3, "Projects did not persist.");
        bool found_unicode_project = false;
        for (const ProjectRecord& project : store.Projects()) {
            std::error_code equivalent_error;
            if (std::filesystem::equivalent(
                    project.local_root, unicode_directory, equivalent_error) &&
                !equivalent_error) {
                found_unicode_project = true;
                break;
            }
        }
        Expect(found_unicode_project, "Unicode project path did not round-trip through JSON.");
        Expect(store.ActiveProjectId() == first_id, "Active project did not persist.");
        const ProjectRecord* second = store.FindById(second_id);
        Expect(second && second->remote_connection_id == "remote-two",
               "Remote connection association did not persist.");
        Expect(store.SetRemoteConnection(second_id, {}, error), error);
        Expect(store.FindById(second_id)->remote_connection_id.empty(),
               "Unlinking a project remote connection failed.");
        Expect(store.SetRemoteConnection(second_id, "remote-two", error), error);
        Expect(store.Remove(first_id, error), error);
        Expect(std::filesystem::is_directory(base / "one"),
               "Removing a project registration must not delete its folder.");
        Expect(store.ActiveProjectId() == second_id,
               "Removing the active project must select a remaining project.");
    }

    std::filesystem::remove_all(base, cleanup_error);
    return 0;
}
