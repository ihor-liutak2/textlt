#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

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

    fs::remove_all(root, cleanup_error);
    return 0;
}
