#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
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
    static void DeleteSelection(NotesWorkspaceComponent& component) {
        component.DeleteSelection();
        component.AcceptConfirmation();
    }
    static void EditSelected(NotesWorkspaceComponent& component) {
        component.EditSelectedNote();
    }
    static bool TrashSelected(const NotesWorkspaceComponent& component) {
        return component.IsTrashSelected();
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
    static void SeedEditorDocument(NotesWorkspaceComponent& component, size_t lines) {
        auto& note = component.repository_.Notes().front();
        note.blocks.clear();
        for (size_t index = 0; index < lines; ++index) {
            NoteBlock block;
            block.id = GenerateUuid();
            block.runs.push_back({
                "Line " + std::to_string(index) +
                    " with plenty of text that must wrap across several visual "
                    "rows inside the note editor viewport.",
                0});
            note.blocks.push_back(std::move(block));
        }
        component.session_.SetNote(&note);
        for (size_t index = 1; index < lines; ++index) component.session_.MoveDown();
    }
    static void RenderWorkspace(NotesWorkspaceComponent& component) {
        RenderWorkspace(component, 100, 28);
    }
    static void RenderWorkspace(NotesWorkspaceComponent& component,
        int width, int height) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, component.Render());
    }
    static size_t EditorRowCount(const NotesWorkspaceComponent& component) {
        return component.editor_rows_.size();
    }
    static size_t NoteBlockCount(const NotesWorkspaceComponent& component) {
        return component.repository_.Notes().front().blocks.size();
    }
    static int EditorScroll(const NotesWorkspaceComponent& component) {
        return component.editor_scroll_;
    }
    static bool Wheel(NotesWorkspaceComponent& component, ftxui::Mouse::Button button) {
        ftxui::Mouse mouse;
        mouse.button = button;
        mouse.motion = ftxui::Mouse::Pressed;
        mouse.x = 60;
        mouse.y = 14;
        return component.OnEvent(ftxui::Event::Mouse("", mouse));
    }
    static ftxui::Box EditorViewBox(const NotesWorkspaceComponent& component) {
        return component.editor_view_box_;
    }
    static ftxui::Box CardBox(const NotesWorkspaceComponent& component, size_t index) {
        return index < component.card_boxes_.size()
            ? component.card_boxes_[index] : ftxui::Box{};
    }
    static size_t CardBoxCount(const NotesWorkspaceComponent& component) {
        return component.card_boxes_.size();
    }
    static void NewNoteWithTitle(NotesWorkspaceComponent& component,
        const std::string& title) {
        component.NewNote();
        for (char character : title) {
            component.OnEvent(ftxui::Event::Character(std::string(1, character)));
        }
        component.OnEvent(ftxui::Event::Escape);
    }
    static bool MouseAt(NotesWorkspaceComponent& component,
        ftxui::Mouse::Button button, ftxui::Mouse::Motion motion, int x, int y) {
        ftxui::Mouse mouse;
        mouse.button = button;
        mouse.motion = motion;
        mouse.x = x;
        mouse.y = y;
        return component.OnEvent(ftxui::Event::Mouse("", mouse));
    }
    static bool HasSelection(const NotesWorkspaceComponent& component) {
        return component.session_.HasSelection();
    }
    static std::string SelectedText(const NotesWorkspaceComponent& component) {
        return component.session_.SelectedText();
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

    // The Notes menu edits the selected overview card and deletes the
    // selection via a single trash-aware action.
    const fs::path menu_root =
        fs::temp_directory_path() / "textlt_notes_menu_actions_test";
    fs::remove_all(menu_root, cleanup_error);
    NotesWorkspaceComponent menu_component(
        nullptr, [](const std::string&) {}, [] {}, [] { return std::string{}; },
        [](const std::string&) {}, menu_root);
    NotesWorkspaceActionsTestAccess::NewNote(menu_component);
    Expect(menu_component.IsEditing(), "New Note should open the note editor.");
    menu_component.OnEvent(ftxui::Event::Escape);
    Expect(!menu_component.IsEditing(), "Escape should return to the note overview.");
    NotesWorkspaceActionsTestAccess::EditSelected(menu_component);
    Expect(menu_component.IsEditing(),
        "Edit Note should open the card selected in the overview.");
    menu_component.OnEvent(ftxui::Event::Escape);
    NotesWorkspaceActionsTestAccess::DeleteSelection(menu_component);
    Expect(NotesWorkspaceActionsTestAccess::FirstNoteDeleted(menu_component),
        "Delete Note should move a selected overview note to Trash.");
    NotesWorkspaceActionsTestAccess::SelectTrash(menu_component);
    Expect(NotesWorkspaceActionsTestAccess::TrashSelected(menu_component),
        "Selecting Trash should report the trash selection to the Notes menu.");
    NotesWorkspaceActionsTestAccess::DeleteSelection(menu_component);
    Expect(NotesWorkspaceActionsTestAccess::NoteCount(menu_component) == 0,
        "Delete in Trash should permanently remove the selected note.");
    fs::remove_all(menu_root, cleanup_error);

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

    // The note editor scrolls over wrapped visual rows and keeps the cursor
    // visible when it moves.
    const fs::path scroll_root =
        fs::temp_directory_path() / "textlt_notes_workspace_scroll_test";
    fs::remove_all(scroll_root, cleanup_error);
    NotesWorkspaceComponent scroll_component(
        nullptr, [](const std::string&) {}, [] {}, [] { return std::string{}; },
        [](const std::string&) {}, scroll_root);
    NotesWorkspaceActionsTestAccess::NewNote(scroll_component);
    NotesWorkspaceActionsTestAccess::SeedEditorDocument(scroll_component, 80);
    NotesWorkspaceActionsTestAccess::RenderWorkspace(scroll_component);
    NotesWorkspaceActionsTestAccess::RenderWorkspace(scroll_component);
    Expect(NotesWorkspaceActionsTestAccess::EditorRowCount(scroll_component) >
            NotesWorkspaceActionsTestAccess::NoteBlockCount(scroll_component),
        "Note blocks should wrap into multiple editor rows.");
    const int bottom_scroll = NotesWorkspaceActionsTestAccess::EditorScroll(scroll_component);
    Expect(bottom_scroll > 0,
        "The note editor should follow the cursor to the bottom of a long note.");
    Expect(NotesWorkspaceActionsTestAccess::Wheel(
            scroll_component, ftxui::Mouse::WheelUp),
        "Wheel up should be handled inside the note editor.");
    NotesWorkspaceActionsTestAccess::RenderWorkspace(scroll_component);
    const int wheel_up_scroll =
        NotesWorkspaceActionsTestAccess::EditorScroll(scroll_component);
    Expect(wheel_up_scroll < bottom_scroll,
        "Wheel up should scroll the note editor upward.");
    Expect(NotesWorkspaceActionsTestAccess::Wheel(
            scroll_component, ftxui::Mouse::WheelDown),
        "Wheel down should be handled inside the note editor.");
    NotesWorkspaceActionsTestAccess::RenderWorkspace(scroll_component);
    const int before_page_up =
        NotesWorkspaceActionsTestAccess::EditorScroll(scroll_component);
    Expect(before_page_up > wheel_up_scroll,
        "Wheel down should scroll the note editor downward.");
    Expect(scroll_component.OnEvent(ftxui::Event::PageUp),
        "Page Up should scroll the note editor.");
    NotesWorkspaceActionsTestAccess::RenderWorkspace(scroll_component);
    const int page_up_scroll =
        NotesWorkspaceActionsTestAccess::EditorScroll(scroll_component);
    Expect(page_up_scroll < before_page_up,
        "Page Up should move the note editor viewport upward.");
    Expect(scroll_component.OnEvent(ftxui::Event::PageDown),
        "Page Down should scroll the note editor.");
    NotesWorkspaceActionsTestAccess::RenderWorkspace(scroll_component);
    Expect(NotesWorkspaceActionsTestAccess::EditorScroll(scroll_component) >
            page_up_scroll,
        "Page Down should move the note editor viewport downward.");

    // Mouse selection: click places the cursor, dragging extends a selection,
    // and releasing keeps it.
    const ftxui::Box view =
        NotesWorkspaceActionsTestAccess::EditorViewBox(scroll_component);
    const int drag_y = view.y_min + 2;
    const int drag_x = view.x_min + 4;
    Expect(NotesWorkspaceActionsTestAccess::MouseAt(
            scroll_component, ftxui::Mouse::Left,
            ftxui::Mouse::Pressed, drag_x, drag_y),
        "Clicking the note body should be handled.");
    Expect(!NotesWorkspaceActionsTestAccess::HasSelection(scroll_component),
        "A click should not select text.");
    Expect(NotesWorkspaceActionsTestAccess::MouseAt(
            scroll_component, ftxui::Mouse::Left,
            ftxui::Mouse::Moved, drag_x + 18, drag_y),
        "Dragging inside the note body should be handled.");
    Expect(NotesWorkspaceActionsTestAccess::HasSelection(scroll_component),
        "Dragging with the left button should select note text.");
    Expect(!NotesWorkspaceActionsTestAccess::SelectedText(scroll_component).empty(),
        "Mouse selection should produce selected text.");
    NotesWorkspaceActionsTestAccess::MouseAt(
        scroll_component, ftxui::Mouse::Left,
        ftxui::Mouse::Released, drag_x + 18, drag_y);
    Expect(NotesWorkspaceActionsTestAccess::HasSelection(scroll_component),
        "Selection should remain after the mouse button is released.");

    NotesWorkspaceActionsTestAccess::MouseAt(
        scroll_component, ftxui::Mouse::Left,
        ftxui::Mouse::Pressed, drag_x, drag_y);
    Expect(!NotesWorkspaceActionsTestAccess::HasSelection(scroll_component),
        "A new click should clear the previous mouse selection.");
    fs::remove_all(scroll_root, cleanup_error);

    // Cards in one overview row must have identical widths. Otherwise long
    // titles/previews make one card wider, overflow the row, and interrupt the
    // neighboring cards' vertical borders.
    const fs::path cards_root =
        fs::temp_directory_path() / "textlt_notes_workspace_cards_test";
    fs::remove_all(cards_root, cleanup_error);
    NotesWorkspaceComponent cards_component(
        nullptr, [](const std::string&) {}, [] {}, [] { return std::string{}; },
        [](const std::string&) {}, cards_root);
    NotesWorkspaceActionsTestAccess::NewNoteWithTitle(cards_component,
        "First card with a title long enough to overflow its preview box");
    NotesWorkspaceActionsTestAccess::NewNoteWithTitle(cards_component,
        "Second card with another long title that used to stretch the layout");
    NotesWorkspaceActionsTestAccess::NewNoteWithTitle(cards_component,
        "Third card with a long title as well so all previews are constrained");
    NotesWorkspaceActionsTestAccess::RenderWorkspace(cards_component, 160, 30);
    NotesWorkspaceActionsTestAccess::RenderWorkspace(cards_component, 160, 30);
    Expect(NotesWorkspaceActionsTestAccess::CardBoxCount(cards_component) == 3,
        "Three notes should produce three overview cards.");
    const ftxui::Box first_card =
        NotesWorkspaceActionsTestAccess::CardBox(cards_component, 0);
    const int first_width = first_card.x_max - first_card.x_min + 1;
    Expect(first_width > 2, "Overview cards should have a visible width.");
    for (size_t index = 1; index < 3; ++index) {
        const ftxui::Box card =
            NotesWorkspaceActionsTestAccess::CardBox(cards_component, index);
        Expect(card.x_max - card.x_min + 1 == first_width,
            "Overview cards in the same row should have equal widths.");
    }
    fs::remove_all(cards_root, cleanup_error);

    fs::remove_all(root, cleanup_error);
    return 0;
}
