#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "notes/notes_workspace_component.hpp"

namespace textlt::notes {

class NotesWorkspaceSyncTestAccess {
public:
    static void RunSync(NotesWorkspaceComponent& component) {
        component.RunSync();
    }

    static bool Completed(NotesWorkspaceComponent& component) {
        std::lock_guard<std::mutex> lock(component.sync_mutex_);
        return component.sync_completed_;
    }

    static void ApplyCompletion(NotesWorkspaceComponent& component) {
        component.ApplySyncCompletion();
    }

    static bool Running(const NotesWorkspaceComponent& component) {
        return component.sync_running_;
    }

    static std::string PopupMessage(NotesWorkspaceComponent& component) {
        std::lock_guard<std::mutex> lock(component.sync_mutex_);
        return component.sync_popup_message_;
    }
};

} // namespace textlt::notes

namespace {

void Expect(bool value, const std::string& message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using textlt::notes::NotesWorkspaceComponent;
    using textlt::notes::NotesWorkspaceSyncTestAccess;

    const fs::path root =
        fs::temp_directory_path() / "textlt_notes_workspace_sync_test";
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);

    NotesWorkspaceComponent component(
        nullptr,
        [](const std::string&) {},
        [] {},
        [] { return std::string{}; },
        [](const std::string&) {},
        [] { return true; },
        [](
            const fs::path&,
            NotesWorkspaceComponent::SyncProgressCallback,
            std::string&) -> bool {
            throw std::runtime_error("simulated sync failure");
        },
        [] {
            throw std::runtime_error("simulated redraw failure");
        },
        root);

    NotesWorkspaceSyncTestAccess::RunSync(component);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!NotesWorkspaceSyncTestAccess::Completed(component) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    Expect(
        NotesWorkspaceSyncTestAccess::Completed(component),
        "Synchronization worker did not report completion.");
    NotesWorkspaceSyncTestAccess::ApplyCompletion(component);
    Expect(
        !NotesWorkspaceSyncTestAccess::Running(component),
        "Synchronization remained active after the failure.");
    Expect(
        NotesWorkspaceSyncTestAccess::PopupMessage(component).find(
            "simulated sync failure") != std::string::npos,
        "Synchronization exception was not shown in the popup.");

    fs::remove_all(root, cleanup_error);
    return 0;
}
