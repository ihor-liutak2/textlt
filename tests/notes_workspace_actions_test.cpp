#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "notes/notes_workspace_component.hpp"

namespace textlt::notes {

class NotesWorkspaceActionsTestAccess {
public:
    static size_t NoteCount(const NotesWorkspaceComponent& component) {
        return component.repository_.Notes().size();
    }
    static size_t SectionCount(const NotesWorkspaceComponent& component) {
        return component.repository_.Sections().size();
    }
    static bool FirstNoteDeleted(const NotesWorkspaceComponent& component) {
        return !component.repository_.Notes().empty() &&
            component.repository_.Notes().front().deleted_at.has_value();
    }
    static std::string FirstNoteSection(const NotesWorkspaceComponent& component) {
        const auto& note = component.repository_.Notes().front();
        return note.section_id ? *note.section_id : std::string{};
    }
    static void NewNote(NotesWorkspaceComponent& component) { component.NewNote(); }
    static void DeleteNote(NotesWorkspaceComponent& component) {
        component.DeleteSelectedNote();
        component.AcceptConfirmation();
    }
    static void SelectTrash(NotesWorkspaceComponent& component) {
        component.editing_ = false;
        component.session_.SetNote(nullptr);
        component.active_note_id_.reset();
        component.SelectSidebarEntry(static_cast<int>(component.sidebar_entries_.size()) - 1);
    }
    static void Restore(NotesWorkspaceComponent& component) { component.RestoreSelectedNote(); }
    static void DeletePermanently(NotesWorkspaceComponent& component) {
        component.PermanentlyDeleteSelectedNote();
        component.AcceptConfirmation();
    }
    static void EmptyTrash(NotesWorkspaceComponent& component) {
        component.EmptyTrash();
        component.AcceptConfirmation();
    }
    static void NewGroup(NotesWorkspaceComponent& component, const std::string& name) {
        component.StartCreateSection();
        component.group_name_input_ = name;
        component.FinishCreateSection();
    }
    static void RenameGroup(NotesWorkspaceComponent& component, const std::string& name) {
        component.StartRenameSection();
        component.group_name_input_ = name;
        component.FinishCreateSection();
    }
    static void DeleteGroup(NotesWorkspaceComponent& component) {
        component.RequestConfirmation(
            NotesWorkspaceComponent::ConfirmAction::DeleteGroup, "Delete group", "Confirm");
        component.AcceptConfirmation();
    }
    static std::string FirstSectionName(const NotesWorkspaceComponent& component) {
        return component.repository_.Sections().front().name;
    }
    static void MoveActiveNoteToFirstGroup(NotesWorkspaceComponent& component) {
        component.OpenNote(0);
        component.OpenGroupPicker();
        component.SelectGroupFromPicker(1);
    }
    static void CycleSort(NotesWorkspaceComponent& component) { component.CycleSortMode(); }
    static bool SortIsCreated(const NotesWorkspaceComponent& component) {
        return component.sort_mode_ == NotesWorkspaceComponent::SortMode::Created;
    }
    static bool CustomEventHandledDuringConfirmation(NotesWorkspaceComponent& component) {
        component.RequestConfirmation(
            NotesWorkspaceComponent::ConfirmAction::DeleteGroup, "Delete group", "Confirm");
        const bool handled = component.OnEvent(ftxui::Event::Custom);
        component.CloseConfirmation();
        return handled;
    }
    static bool CustomEventHandledDuringGroupPicker(NotesWorkspaceComponent& component) {
        component.OpenGroupPicker();
        const bool handled = component.OnEvent(ftxui::Event::Custom);
        component.group_picker_open_ = false;
        return handled;
    }
    static void SetStaleOverviewHotspots(NotesWorkspaceComponent& component) {
        // These regions belong to the overview screen. They must not react
        // while the editor is open, even though their boxes are stale.
        const ftxui::Box stale{10, 20, 10, 10};
        component.new_note_box_ = stale;
        component.search_box_ = stale;
        component.sort_box_ = stale;
    }
    static bool Click(NotesWorkspaceComponent& component, int x, int y) {
        ftxui::Mouse mouse;
        mouse.button = ftxui::Mouse::Left;
        mouse.motion = ftxui::Mouse::Pressed;
        mouse.x = x;
        mouse.y = y;
        return component.OnEvent(ftxui::Event::Mouse("", mouse));
    }
    static bool SearchFocused(const NotesWorkspaceComponent& component) {
        return component.focus_ == NotesWorkspaceComponent::FocusArea::Search;
    }
    static void SetProject(NotesWorkspaceComponent& component,
        const std::string& id,
        const std::string& name,
        std::vector<std::string> known_ids) {
        component.SetProjectContext(ProjectContext{id, name}, std::move(known_ids));
    }
    static void ClearProject(NotesWorkspaceComponent& component,
        std::vector<std::string> known_ids) {
        component.SetProjectContext(std::nullopt, std::move(known_ids));
    }
    static bool HasProjectContext(const NotesWorkspaceComponent& component) {
        return component.project_context_.has_value();
    }
    static std::string ProjectSectionId(
        const NotesWorkspaceComponent& component,
        const std::string& project_id) {
        const NoteSection* section = component.repository_.FindProjectSection(project_id);
        return section ? section->id : std::string{};
    }
    static std::string SectionNameById(
        const NotesWorkspaceComponent& component,
        const std::string& id) {
        for (const NoteSection& section : component.repository_.Sections()) {
            if (section.id == id) return section.name;
        }
        return {};
    }
    static bool SectionDeleted(
        const NotesWorkspaceComponent& component,
        const std::string& id) {
        for (const NoteSection& section : component.repository_.Sections()) {
            if (section.id == id) return section.deleted_at.has_value();
        }
        return false;
    }
    static std::string SidebarIdAt(const NotesWorkspaceComponent& component, size_t index) {
        if (index >= component.sidebar_entries_.size()) return {};
        return component.sidebar_entries_[index].id;
    }
    static bool SidebarEntryIsHome(const NotesWorkspaceComponent& component, size_t index) {
        return index < component.sidebar_entries_.size() &&
            component.sidebar_entries_[index].kind ==
                NotesWorkspaceComponent::SidebarEntryKind::Home;
    }
    static bool SidebarEntryIsProject(
        const NotesWorkspaceComponent& component, size_t index) {
        return index < component.sidebar_entries_.size() &&
            component.sidebar_entries_[index].project;
    }
    static bool SidebarEntryIsOrphaned(
        const NotesWorkspaceComponent& component, size_t index) {
        return index < component.sidebar_entries_.size() &&
            component.sidebar_entries_[index].orphaned;
    }
    static bool SectionIsOrphaned(
        const NotesWorkspaceComponent& component, const std::string& id) {
        for (const auto& entry : component.sidebar_entries_) {
            if (entry.kind == NotesWorkspaceComponent::SidebarEntryKind::Section &&
                entry.id == id) {
                return entry.orphaned;
            }
        }
        return false;
    }
    static bool SectionIsProject(
        const NotesWorkspaceComponent& component, const std::string& id) {
        for (const auto& entry : component.sidebar_entries_) {
            if (entry.kind == NotesWorkspaceComponent::SidebarEntryKind::Section &&
                entry.id == id) {
                return entry.project;
            }
        }
        return false;
    }
    static void SelectGroupById(NotesWorkspaceComponent& component, const std::string& id) {
        component.SelectSectionById(id);
    }
    static std::string SelectedSectionId(const NotesWorkspaceComponent& component) {
        return component.selected_section_id_;
    }
    static void OpenProjectNotes(NotesWorkspaceComponent& component) {
        component.OpenProjectNotes();
    }
    static void ConvertSelectedToGeneral(NotesWorkspaceComponent& component) {
        component.ConvertSelectedGroupToGeneral();
    }
    static void RelinkSelectedToActiveProject(NotesWorkspaceComponent& component) {
        component.RelinkSelectedGroupToActiveProject();
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
    using textlt::notes::NotesWorkspaceActionsTestAccess;
    using textlt::notes::NotesWorkspaceComponent;

    const fs::path root = fs::temp_directory_path() / "textlt_notes_workspace_actions_test";
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);

    NotesWorkspaceComponent component(
        nullptr, [](const std::string&) {}, [] {}, [] { return std::string{}; },
        [](const std::string&) {}, root);

    NotesWorkspaceActionsTestAccess::NewNote(component);
    Expect(NotesWorkspaceActionsTestAccess::NoteCount(component) == 1,
        "New Note footer action did not create a note.");
    Expect(!NotesWorkspaceActionsTestAccess::CustomEventHandledDuringConfirmation(component),
        "Confirmation popup consumed a redraw event and caused an endless redraw loop.");
    Expect(!NotesWorkspaceActionsTestAccess::CustomEventHandledDuringGroupPicker(component),
        "Group picker consumed a redraw event and caused an endless redraw loop.");
    NotesWorkspaceActionsTestAccess::SetStaleOverviewHotspots(component);
    NotesWorkspaceActionsTestAccess::Click(component, 15, 10);
    Expect(NotesWorkspaceActionsTestAccess::NoteCount(component) == 1,
        "A stale overview hotspot created a note while the editor was open.");
    Expect(!NotesWorkspaceActionsTestAccess::SearchFocused(component),
        "A stale overview hotspot moved focus to search while the editor was open.");
    NotesWorkspaceActionsTestAccess::DeleteNote(component);
    Expect(NotesWorkspaceActionsTestAccess::FirstNoteDeleted(component),
        "Delete Note footer action did not move the note to Trash.");

    NotesWorkspaceActionsTestAccess::SelectTrash(component);
    NotesWorkspaceActionsTestAccess::Restore(component);
    Expect(!NotesWorkspaceActionsTestAccess::FirstNoteDeleted(component),
        "Restore footer action did not restore the note.");

    NotesWorkspaceActionsTestAccess::NewGroup(component, "Work");
    Expect(NotesWorkspaceActionsTestAccess::SectionCount(component) == 1,
        "New Group footer action did not create a group.");
    NotesWorkspaceActionsTestAccess::RenameGroup(component, "Projects");
    Expect(NotesWorkspaceActionsTestAccess::FirstSectionName(component) == "Projects",
        "Rename Group footer action did not rename the group.");
    NotesWorkspaceActionsTestAccess::MoveActiveNoteToFirstGroup(component);
    Expect(!NotesWorkspaceActionsTestAccess::FirstNoteSection(component).empty(),
        "Direct group selection did not move the note.");
    NotesWorkspaceActionsTestAccess::DeleteGroup(component);
    Expect(NotesWorkspaceActionsTestAccess::FirstNoteSection(component).empty(),
        "Delete Group did not return its notes to Home.");

    NotesWorkspaceActionsTestAccess::CycleSort(component);
    Expect(NotesWorkspaceActionsTestAccess::SortIsCreated(component),
        "Sort control did not advance to Created.");

    NotesWorkspaceActionsTestAccess::DeleteNote(component);
    NotesWorkspaceActionsTestAccess::SelectTrash(component);
    NotesWorkspaceActionsTestAccess::DeletePermanently(component);
    Expect(NotesWorkspaceActionsTestAccess::NoteCount(component) == 0,
        "Delete Permanently did not remove the trashed note.");

    NotesWorkspaceActionsTestAccess::NewNote(component);
    NotesWorkspaceActionsTestAccess::DeleteNote(component);
    NotesWorkspaceActionsTestAccess::SelectTrash(component);
    NotesWorkspaceActionsTestAccess::EmptyTrash(component);
    Expect(NotesWorkspaceActionsTestAccess::NoteCount(component) == 0,
        "Empty Trash did not remove all trashed notes.");

    // Project-linked groups.
    const std::vector<std::string> known_ab = {"project-a", "project-b"};
    NotesWorkspaceActionsTestAccess::NewGroup(component, "Alpha");
    NotesWorkspaceActionsTestAccess::SetProject(component, "project-a", "Alpha", known_ab);
    const std::string alpha_id =
        NotesWorkspaceActionsTestAccess::ProjectSectionId(component, "project-a");
    Expect(!alpha_id.empty(), "Active project did not get a notes group.");
    Expect(NotesWorkspaceActionsTestAccess::SectionNameById(component, alpha_id) == "Alpha (2)",
        "A project group should not silently adopt a General group with the same name.");
    Expect(NotesWorkspaceActionsTestAccess::SidebarEntryIsHome(component, 0),
        "Home should remain the first sidebar entry.");
    Expect(NotesWorkspaceActionsTestAccess::SidebarIdAt(component, 1) == alpha_id,
        "The active project group should be the first group.");
    Expect(NotesWorkspaceActionsTestAccess::SidebarEntryIsProject(component, 1),
        "The project group should be marked as a project group.");

    const size_t section_count = NotesWorkspaceActionsTestAccess::SectionCount(component);
    NotesWorkspaceActionsTestAccess::SetProject(component, "project-a", "Alpha", known_ab);
    Expect(NotesWorkspaceActionsTestAccess::SectionCount(component) == section_count,
        "Project context should create the project group only once.");
    Expect(NotesWorkspaceActionsTestAccess::ProjectSectionId(component, "project-a") == alpha_id,
        "Reapplying the same project context should reuse its group.");

    NotesWorkspaceActionsTestAccess::SetProject(component, "project-b", "Beta", known_ab);
    const std::string beta_id =
        NotesWorkspaceActionsTestAccess::ProjectSectionId(component, "project-b");
    Expect(!beta_id.empty() && beta_id != alpha_id,
        "Switching projects should create a distinct project group.");
    Expect(NotesWorkspaceActionsTestAccess::SidebarIdAt(component, 1) == beta_id,
        "The newly active project group should move to the top.");
    NotesWorkspaceActionsTestAccess::SetProject(component, "project-a", "Alpha", known_ab);
    Expect(NotesWorkspaceActionsTestAccess::SidebarIdAt(component, 1) == alpha_id,
        "Switching back should move the original project group to the top.");

    const size_t count_before_open = NotesWorkspaceActionsTestAccess::SectionCount(component);
    NotesWorkspaceActionsTestAccess::OpenProjectNotes(component);
    Expect(NotesWorkspaceActionsTestAccess::SectionCount(component) == count_before_open,
        "Open Project Notes should be idempotent.");
    Expect(NotesWorkspaceActionsTestAccess::SelectedSectionId(component) == alpha_id,
        "Open Project Notes should select the active project group.");

    // Removing a project from the registry keeps its notes and marks its
    // group as orphaned.
    NotesWorkspaceActionsTestAccess::SetProject(component, "project-a", "Alpha", {"project-a"});
    Expect(NotesWorkspaceActionsTestAccess::SectionIsOrphaned(component, beta_id),
        "A group for a removed project should be marked as orphaned.");
    Expect(!NotesWorkspaceActionsTestAccess::ProjectSectionId(component, "project-b").empty(),
        "Removing a project must not delete its notes group.");

    // Relink the orphan into the active project, which already owns a group.
    NotesWorkspaceActionsTestAccess::SelectGroupById(component, beta_id);
    NotesWorkspaceActionsTestAccess::NewNote(component);
    NotesWorkspaceActionsTestAccess::RelinkSelectedToActiveProject(component);
    Expect(NotesWorkspaceActionsTestAccess::SectionDeleted(component, beta_id),
        "Relinking into an existing project group should retire the orphan.");
    Expect(NotesWorkspaceActionsTestAccess::FirstNoteSection(component) == alpha_id,
        "Relinking should move orphaned notes into the project group.");
    Expect(NotesWorkspaceActionsTestAccess::ProjectSectionId(component, "project-b").empty(),
        "The retired orphan should no longer be the project's group.");

    // A group can be preserved as a General group after its project is gone.
    NotesWorkspaceActionsTestAccess::SetProject(
        component, "project-c", "Gamma", {"project-a", "project-c"});
    const std::string gamma_id =
        NotesWorkspaceActionsTestAccess::ProjectSectionId(component, "project-c");
    Expect(!gamma_id.empty(), "Switching to another project did not create its group.");
    NotesWorkspaceActionsTestAccess::SetProject(component, "project-a", "Alpha", {"project-a"});
    Expect(NotesWorkspaceActionsTestAccess::SectionIsOrphaned(component, gamma_id),
        "The removed project's group should be orphaned.");
    NotesWorkspaceActionsTestAccess::SelectGroupById(component, gamma_id);
    NotesWorkspaceActionsTestAccess::ConvertSelectedToGeneral(component);
    Expect(!NotesWorkspaceActionsTestAccess::SectionIsProject(component, gamma_id),
        "Converting an orphan should turn it into a General group.");
    Expect(!NotesWorkspaceActionsTestAccess::SectionIsOrphaned(component, gamma_id),
        "A converted group should no longer be orphaned.");

    // Non-project folders have no project context and therefore no Projects
    // footer block.
    NotesWorkspaceActionsTestAccess::ClearProject(component, {"project-a"});
    Expect(!NotesWorkspaceActionsTestAccess::HasProjectContext(component),
        "Opening a non-project folder should clear the project context.");
    const size_t count_without_project = NotesWorkspaceActionsTestAccess::SectionCount(component);
    NotesWorkspaceActionsTestAccess::OpenProjectNotes(component);
    Expect(NotesWorkspaceActionsTestAccess::SectionCount(component) == count_without_project,
        "Open Project Notes without an active project must not create a group.");

    fs::remove_all(root, cleanup_error);
    return 0;
}
