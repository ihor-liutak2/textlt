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
#include "theme.hpp"

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
        component.RequestDeleteSelectedSection();
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
    static void SetStaleSidebarHotspot(NotesWorkspaceComponent& component) {
        component.footer_new_note_box_ = ftxui::Box{10, 20, 10, 10};
    }
    static void ClearStaleSidebarHotspot(NotesWorkspaceComponent& component) {
        component.footer_new_note_box_ = ftxui::Box{1, 0, 1, 0};
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
                    "rows inside the note editor viewport."});
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
    static std::string RenderWorkspaceText(NotesWorkspaceComponent& component) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(100), ftxui::Dimension::Fixed(28));
        ftxui::Render(screen, component.Render());
        return screen.ToString();
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
    static ftxui::Box EditorScrollbarBox(const NotesWorkspaceComponent& component) {
        return component.editor_scrollbar_box_;
    }
    static ftxui::Box SidebarBox(const NotesWorkspaceComponent& component) {
        return component.sidebar_box_;
    }
    static NoteSession& Session(NotesWorkspaceComponent& component) {
        return component.session_;
    }
    static void SetEditorColumns(NotesWorkspaceComponent& component, int columns) {
        component.SetEditorColumns(columns);
    }
    static int EditorColumns(const NotesWorkspaceComponent& component) {
        return component.EditorColumns();
    }
    static size_t EditorColumnCount(const NotesWorkspaceComponent& component) {
        return component.editor_column_boxes_.size();
    }
    static ftxui::Box EditorColumnBox(
        const NotesWorkspaceComponent& component, size_t index) {
        return index < component.editor_column_boxes_.size()
            ? component.editor_column_boxes_[index] : ftxui::Box{};
    }
    static size_t EditorColumnRows(const NotesWorkspaceComponent& component) {
        return component.editor_column_rows_;
    }
    static size_t EditorPageRows(const NotesWorkspaceComponent& component) {
        return component.editor_visible_rows_;
    }
    static size_t CursorBlock(const NotesWorkspaceComponent& component) {
        return component.session_.Cursor().block;
    }
    static void SeedHeadingDocument(NotesWorkspaceComponent& component) {
        NoteDocument& note = component.repository_.Notes().front();
        note.blocks.clear();
        for (int level = 1; level <= 3; ++level) {
            NoteBlock block;
            block.id = GenerateUuid();
            block.heading = level;
            block.runs.push_back({"Heading " + std::to_string(level)});
            note.blocks.push_back(std::move(block));
        }
        component.session_.SetNote(&note);
        const NoteBlock& last = note.blocks.back();
        component.session_.SetCursor(
            {note.blocks.size() - 1, BlockText(last).size()});
    }
    static int BlockHeading(
        const NotesWorkspaceComponent& component, size_t block) {
        const NoteDocument* note = component.session_.Note();
        return note && block < note->blocks.size()
            ? note->blocks[block].heading : 0;
    }
    static int ActiveHeading(const NotesWorkspaceComponent& component) {
        return component.session_.ActiveHeading();
    }
    static ftxui::Box HeadingBox(
        const NotesWorkspaceComponent& component, int level) {
        if (level == 1) return component.heading_1_box_;
        if (level == 2) return component.heading_2_box_;
        if (level == 3) return component.heading_3_box_;
        return {};
    }
    static ftxui::Box CopyBox(const NotesWorkspaceComponent& component) {
        return component.copy_box_;
    }
    static void SetFirstBlockText(
        NotesWorkspaceComponent& component, const std::string& text) {
        NoteDocument& note = component.repository_.Notes().front();
        if (note.blocks.empty()) return;
        note.blocks.front().runs = {{text}};
        component.session_.SetNote(&note);
        component.session_.SetCursor({0, text.size() / 2});
    }
    static ftxui::Screen RenderWorkspaceScreen(
        NotesWorkspaceComponent& component, int width, int height) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, component.Render());
        return screen;
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
        if (!component.repository_.Notes().empty()) {
            NoteDocument& note = component.repository_.Notes().front();
            note.title = title;
            std::string error;
            component.repository_.Save(note, error);
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
    static bool SidebarFocused(const NotesWorkspaceComponent& component) {
        return component.focus_ == NotesWorkspaceComponent::FocusArea::Sidebar;
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
    static bool RequestDeleteGroup(NotesWorkspaceComponent& component) {
        component.RequestDeleteSelectedSection();
        return component.confirm_action_ == NotesWorkspaceComponent::ConfirmAction::DeleteGroup;
    }
    static void CancelDeleteGroup(NotesWorkspaceComponent& component) {
        component.CloseConfirmation();
    }
    static void RelinkSelectedToActiveProject(NotesWorkspaceComponent& component) {
        component.RelinkSelectedGroupToActiveProject();
    }
};

} // namespace textlt::notes

namespace {

void Expect(bool value, const std::string& message) {
    std::cerr << "Check: " << message << '\n';
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

std::pair<int, int> FindMenuLabel(const ftxui::Screen& screen, const std::string& label,
                                  std::optional<ftxui::Box> limit = std::nullopt) {
    const auto box = limit.value_or(ftxui::Box{0, screen.dimx() - 1, 0, screen.dimy() - 1});
    for (int y = box.y_min; y <= box.y_max; ++y) {
        for (int x = box.x_min + 2; x + static_cast<int>(label.size()) <= box.x_max; ++x) {
            if (screen.PixelAt(x - 2, y).character != "│" ||
                screen.PixelAt(x - 1, y).character != " ") continue;
            std::string text;
            for (size_t index = 0; index < label.size(); ++index) {
                text += screen.PixelAt(x + static_cast<int>(index), y).character;
            }
            if (text == label) return {x, y};
        }
    }
    Expect(false, "Missing menu label " + label + "\n" + screen.ToString());
    return {};
}

void TestNotesContextMenu(const std::filesystem::path& root) {
    using namespace textlt::notes;
    using Access = NotesWorkspaceActionsTestAccess;
    using ftxui::Event;
    using ftxui::Mouse;

    std::string clipboard;
    NotesWorkspaceComponent workspace(
        nullptr, {}, {}, {}, [&](const std::string& text) { clipboard = text; return true; }, root);
    workspace.SetClipboardReader([&](textlt::ClipboardController::PasteCallback deliver) {
        deliver(clipboard);
    });
    const auto render = [&](int width = 104, int height = 30) {
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
        ftxui::Render(screen, ftxui::vbox({
            ftxui::text("Application header"),
            workspace.Render() | ftxui::flex,
            ftxui::text("Application footer"),
        }));
        return screen;
    };
    const auto mouse = [&](Mouse::Button button, Mouse::Motion motion, int x, int y) {
        return Access::MouseAt(workspace, button, motion, x, y);
    };
    const auto right_click = [&](int x, int y) {
        return mouse(Mouse::Right, Mouse::Pressed, x, y);
    };
    const auto menu_rectangle = [&](const ftxui::Screen& screen) {
        const auto [label_x, label_y] = FindMenuLabel(screen, "Undo");
        const int left = label_x - 2;
        const int top = label_y - 1;
        const int right = left + static_cast<int>(std::string("Numbered List").size()) + 3;
        const int bottom = top + 16;
        Expect(left >= 0 && top > 0 && right < screen.dimx() && bottom < screen.dimy() - 1 &&
                screen.PixelAt(left, top).character == "┌" &&
                screen.PixelAt(right, bottom).character == "┘", "Notes menu stays within workspace and terminal bounds");
        return ftxui::Box{left, right, top, bottom};
    };

    render();
    Expect(!right_click(50, 10) && !workspace.HasOpenContextMenu(), "Notes overview never opens the editor menu");
    Access::NewNote(workspace);
    Access::SeedEditorDocument(workspace, 97);
    auto& session = Access::Session(workspace);
    session.SetCursor({0, 0});
    session.SetCursor({0, 4}, true);
    const auto original_selection = session.Selection();
    const auto original_cursor = session.Cursor();
    const auto original_revision = session.Note()->revision;
    const auto original_note = session.Note()->id;

    for (int columns : {2, 3}) {
        workspace.SetEditorColumns(columns);
        render();
        render();
        const auto first = Access::EditorColumnBox(workspace, 0);
        const auto last = Access::EditorColumnBox(workspace, columns - 1);
        const auto sidebar = Access::SidebarBox(workspace);
        const auto scrollbar = Access::EditorScrollbarBox(workspace);
        const auto view = Access::EditorViewBox(workspace);
        for (const auto& [x, y] : {std::pair{sidebar.x_min + 2, sidebar.y_min + 3},
                 std::pair{scrollbar.x_min, scrollbar.y_min},
                 std::pair{first.x_min, view.y_min},
                 std::pair{first.x_min, view.y_min - 1},
                 std::pair{first.x_max + 1, first.y_min + 1}}) {
            right_click(x, y);
            Expect(!workspace.HasOpenContextMenu(), "Notes menu excludes sidebar, toolbar, border, scrollbar and dividers");
        }
        for (bool right : {false, true}) {
            for (bool bottom : {false, true}) {
                const int x = right ? last.x_max : first.x_min;
                const int y = bottom ? first.y_max : first.y_min;
                Expect(right_click(x, y) && workspace.HasOpenContextMenu(), "Right-click opens in each editor corner");
                const auto popup = menu_rectangle(render());
                Expect((right ? popup.x_max == x : popup.x_min == x) &&
                       (bottom ? popup.y_max == y : popup.y_min == y), "Menu opens inward from the editor corner");
                Expect(session.Cursor() == original_cursor && session.Selection() == original_selection &&
                    session.Note()->revision == original_revision && session.Note()->id == original_note &&
                    workspace.EditorColumns() == columns, "Opening preserves the entire Notes editing context");
                Expect(workspace.OnEvent(Event::Escape) && !workspace.HasOpenContextMenu() &&
                    workspace.IsEditing(), "Escape dismisses only the Notes menu");
            }
        }
        Expect(right_click(first.x_max, first.y_min), "Right-click at internal column edge");
        auto frame = render();
        Expect(menu_rectangle(frame).x_max > first.x_max &&
               frame.ToString().find("Numbered List") != std::string::npos &&
               frame.ToString().find("Bold") == std::string::npos &&
               frame.ToString().find("Italic") == std::string::npos,
               "The full Notes menu can draw across columns without Bold or Italic");
        Expect(!workspace.OnEvent(Event::Special({0})) && workspace.HasOpenContextMenu(),
               "Terminal resize event leaves the menu open");
        menu_rectangle(render(65, 22));
        workspace.OnEvent(Event::ArrowDown);
        render(40, 10);
        Expect(workspace.HasOpenContextMenu(), "The menu remains navigable on a short screen");
        Expect(mouse(Mouse::Left, Mouse::Pressed, 0, 0) && !workspace.HasOpenContextMenu(),
               "Clicking outside dismisses the menu without moving the selection");
        Expect(session.Selection() == original_selection, "Outside dismissal preserves the selection");
    }

    render();
    render();
    const auto menu = [&](const std::string& label, bool hover = false) {
        const auto first = Access::EditorColumnBox(workspace, 0);
        Expect(right_click(first.x_min, first.y_min), "Open Notes menu for command test");
        const auto screen = render();
        const auto [x, y] = FindMenuLabel(screen, label, menu_rectangle(screen));
        if (hover) {
            Expect(mouse(Mouse::None, Mouse::Moved, x, y), "Menu handles hover");
        } else {
            Expect(mouse(Mouse::Left, Mouse::Pressed, x, y) &&
                mouse(Mouse::Left, Mouse::Released, x, y), "Menu handles left click");
        }
    };

    const std::string original_first = BlockText(session.Note()->blocks[0]);
    menu("Copy");
    Expect(clipboard == "Line" && !workspace.HasOpenContextMenu() &&
        session.Selection() == original_selection, "Menu Copy uses the preserved selection");
    menu("Cut");
    Expect(BlockText(session.Note()->blocks[0]) == original_first.substr(4),
        "Menu Cut removes only the copied selection");
    menu("Undo");
    Expect(BlockText(session.Note()->blocks[0]) == original_first, "Menu Undo restores cut text");
    menu("Redo");
    Expect(BlockText(session.Note()->blocks[0]) == original_first.substr(4), "Menu Redo reapplies cut");
    clipboard = "Line";
    menu("Paste");
    Expect(BlockText(session.Note()->blocks[0]) == original_first, "Menu Paste inserts at the retained cursor");

    session.SetCursor({1, 3});
    menu("Heading 1");
    Expect(session.Note()->blocks[0].heading == 0 && session.Note()->blocks[1].heading == 1,
        "Heading changes only the current paragraph without a selection");
    session.SetCursor({0, 3});
    session.SetCursor({2, 0}, true);
    menu("Heading 2", true);
    Expect(workspace.OnEvent(Event::ArrowDown) && workspace.OnEvent(Event::Return) &&
        !workspace.HasOpenContextMenu(), "Hover, Down, and Enter run the focused command");
    Expect(session.Note()->blocks[0].heading == 3 && session.Note()->blocks[1].heading == 3 &&
        session.Note()->blocks[2].heading == 0, "Heading formats only affected paragraphs");
    const auto before_no_op = session.Note()->revision;
    menu("Heading 3");
    Expect(workspace.HasOpenContextMenu() && session.Note()->revision == before_no_op,
           "An already-applied heading is disabled");
    workspace.OnEvent(Event::Escape);
    menu("Bulleted List");
    Expect(session.Note()->blocks[0].type == NoteBlockType::BulletItem &&
        session.Note()->blocks[1].type == NoteBlockType::BulletItem &&
        session.Note()->blocks[2].type == NoteBlockType::Paragraph, "Bullets format both selected blocks");
    menu("Numbered List");
    Expect(session.Note()->blocks[0].type == NoteBlockType::NumberedItem &&
        session.Note()->blocks[1].type == NoteBlockType::NumberedItem, "Numbered list formats the selection");
    menu("Normal Text");
    Expect(session.Note()->blocks[0].type == NoteBlockType::Paragraph &&
        session.Note()->blocks[1].type == NoteBlockType::Paragraph, "Normal Text removes list formatting");
    menu("Undo");
    Expect(session.Note()->blocks[0].type == NoteBlockType::NumberedItem &&
        session.Note()->blocks[1].type == NoteBlockType::NumberedItem, "Undo restores both formatted blocks at once");
    menu("Select All");
    Expect(session.HasSelection() && session.Selection().first == NotePosition{0, 0} &&
        session.Selection().second.block == session.Note()->blocks.size() - 1, "Menu Select All uses the Notes session");
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using textlt::notes::NotesWorkspaceActionsTestAccess;
    using textlt::notes::NotesWorkspaceComponent;

    const fs::path root = fs::temp_directory_path() / "textlt_notes_workspace_actions_test";
    std::error_code cleanup_error;
    fs::remove_all(root, cleanup_error);
    TestNotesContextMenu(root / "context-menu");

    NotesWorkspaceComponent component(
        nullptr, [](const std::string&) {}, [] {}, [] {},
        [](const std::string&) { return true; }, root);

    NotesWorkspaceActionsTestAccess::NewNote(component);
    Expect(NotesWorkspaceActionsTestAccess::NoteCount(component) == 1,
        "New Note footer action did not create a note.");
    Expect(!NotesWorkspaceActionsTestAccess::CustomEventHandledDuringConfirmation(component),
        "Confirmation popup consumed a redraw event and caused an endless redraw loop.");
    Expect(!NotesWorkspaceActionsTestAccess::CustomEventHandledDuringGroupPicker(component),
        "Group picker consumed a redraw event and caused an endless redraw loop.");
    component.FocusSidebar();
    Expect(NotesWorkspaceActionsTestAccess::SidebarFocused(component),
        "FocusSidebar should focus the visible Notes sidebar.");
    component.SetSidebarVisible(false);
    Expect(!component.IsSidebarVisible(),
        "SetSidebarVisible(false) should hide the Notes sidebar.");
    Expect(!NotesWorkspaceActionsTestAccess::SidebarFocused(component),
        "Hiding the Notes sidebar should move focus out of it.");
    const size_t notes_before_hidden_sidebar_click =
        NotesWorkspaceActionsTestAccess::NoteCount(component);
    NotesWorkspaceActionsTestAccess::SetStaleSidebarHotspot(component);
    NotesWorkspaceActionsTestAccess::Click(component, 15, 10);
    Expect(NotesWorkspaceActionsTestAccess::NoteCount(component) ==
            notes_before_hidden_sidebar_click,
        "A stale sidebar hotspot created a note while the sidebar was hidden.");
    NotesWorkspaceActionsTestAccess::ClearStaleSidebarHotspot(component);
    component.SetSidebarVisible(true);
    Expect(component.IsSidebarVisible(),
        "SetSidebarVisible(true) should show the Notes sidebar again.");
    component.FocusSidebar();
    Expect(NotesWorkspaceActionsTestAccess::SidebarFocused(component),
        "The Notes sidebar should be focusable after being shown again.");
    component.FocusMainArea();
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
        nullptr, [](const std::string&) {}, [] {}, [] {},
        [](const std::string&) { return true; }, menu_root);
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

    // A group loaded with an unknown project ID is offered for relinking.
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
    Expect(!NotesWorkspaceActionsTestAccess::RequestDeleteGroup(component),
        "A registered project's group should be protected from deletion.");
    NotesWorkspaceActionsTestAccess::SetProject(component, "project-a", "Alpha", {"project-a"});
    Expect(NotesWorkspaceActionsTestAccess::SectionIsOrphaned(component, gamma_id),
        "The removed project's group should be orphaned.");
    NotesWorkspaceActionsTestAccess::SelectGroupById(component, gamma_id);
    Expect(NotesWorkspaceActionsTestAccess::RequestDeleteGroup(component),
        "Delete should open the confirmation for an orphaned group.");
    NotesWorkspaceActionsTestAccess::CancelDeleteGroup(component);
    Expect(!NotesWorkspaceActionsTestAccess::SectionIsProject(component, gamma_id),
        "Requesting deletion of an orphan should turn it into a General group.");
    Expect(!NotesWorkspaceActionsTestAccess::SectionIsOrphaned(component, gamma_id),
        "A converted group should no longer be orphaned.");
    Expect(!NotesWorkspaceActionsTestAccess::SectionDeleted(component, gamma_id),
        "Canceling deletion should keep the converted group.");

    // Explicit project removal detaches the group and keeps its notes.
    NotesWorkspaceActionsTestAccess::SetProject(
        component, "project-d", "Delta", {"project-a", "project-d"});
    const std::string delta_id =
        NotesWorkspaceActionsTestAccess::ProjectSectionId(component, "project-d");
    NotesWorkspaceActionsTestAccess::NewNote(component);
    std::string detach_error;
    Expect(component.DetachProjectGroup("project-d", detach_error), detach_error);
    NotesWorkspaceActionsTestAccess::SetProject(component, "project-a", "Alpha", {"project-a"});
    Expect(!NotesWorkspaceActionsTestAccess::SectionIsProject(component, delta_id),
        "The removed project's Notes group should be a regular group.");
    Expect(NotesWorkspaceActionsTestAccess::FirstNoteSection(component) == delta_id,
        "Detaching a project group should keep its notes in the group.");
    NotesWorkspaceActionsTestAccess::SelectGroupById(component, delta_id);
    NotesWorkspaceActionsTestAccess::DeleteGroup(component);
    Expect(NotesWorkspaceActionsTestAccess::SectionDeleted(component, delta_id),
        "The detached Notes group should be deletable.");

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
        nullptr, [](const std::string&) {}, [] {}, [] {},
        [](const std::string&) { return true; }, scroll_root);
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

    // Column view: clamp to 1..3, render one reflected box per column, and
    // map clicks in later columns to later visual rows.
    const size_t single_column_rows =
        NotesWorkspaceActionsTestAccess::EditorRowCount(scroll_component);
    NotesWorkspaceActionsTestAccess::SetEditorColumns(scroll_component, 0);
    Expect(NotesWorkspaceActionsTestAccess::EditorColumns(scroll_component) == 1,
        "Column count should clamp to at least one.");
    NotesWorkspaceActionsTestAccess::SetEditorColumns(scroll_component, 4);
    Expect(NotesWorkspaceActionsTestAccess::EditorColumns(scroll_component) == 3,
        "Column count should clamp to at most three.");
    NotesWorkspaceActionsTestAccess::SetEditorColumns(scroll_component, 2);
    NotesWorkspaceActionsTestAccess::RenderWorkspace(scroll_component);
    const std::string two_column_text =
        NotesWorkspaceActionsTestAccess::RenderWorkspaceText(scroll_component);
    Expect(NotesWorkspaceActionsTestAccess::EditorColumnCount(scroll_component) == 2,
        "Two-column mode should render two editor columns.");
    Expect(two_column_text.find("·") != std::string::npos,
        "Two-column mode should render dotted column separators.");
    Expect(NotesWorkspaceActionsTestAccess::EditorColumnRows(scroll_component) > 0,
        "Two-column mode should compute a non-empty column height.");
    Expect(NotesWorkspaceActionsTestAccess::EditorPageRows(scroll_component) ==
            NotesWorkspaceActionsTestAccess::EditorColumnRows(scroll_component) * 2,
        "Two-column mode should use two column heights as the scroll page size.");
    Expect(NotesWorkspaceActionsTestAccess::EditorRowCount(scroll_component) >
            single_column_rows,
        "Narrower columns should wrap the note into more visual rows.");
    const ftxui::Box first_column =
        NotesWorkspaceActionsTestAccess::EditorColumnBox(scroll_component, 0);
    const ftxui::Box second_column =
        NotesWorkspaceActionsTestAccess::EditorColumnBox(scroll_component, 1);
    Expect(second_column.x_max >= second_column.x_min &&
            second_column.y_max >= second_column.y_min,
        "The second editor column should have a visible hit box.");
    Expect(second_column.x_min >= first_column.x_max + 3,
        "A separator and one blank cell should sit before the second column text.");
    Expect(NotesWorkspaceActionsTestAccess::MouseAt(
            scroll_component, ftxui::Mouse::Left,
            ftxui::Mouse::Pressed,
            second_column.x_min + 2, second_column.y_min + 1),
        "Clicking the second editor column should be handled.");
    Expect(NotesWorkspaceActionsTestAccess::CursorBlock(scroll_component) > 0,
        "Clicking the second editor column should move the cursor past the first block.");

    fs::remove_all(scroll_root, cleanup_error);

    // Headings: H1 mirrors action-title rows (accent background, contrasting
    // foreground, bold); H2/H3 keep the background and use bold theme accent
    // text. The toolbar buttons toggle the heading on the active block.
    const fs::path heading_root =
        fs::temp_directory_path() / "textlt_notes_workspace_heading_test";
    fs::remove_all(heading_root, cleanup_error);
    NotesWorkspaceComponent heading_component(
        nullptr, [](const std::string&) {}, [] {}, [] {},
        [](const std::string&) { return true; }, heading_root);
    NotesWorkspaceActionsTestAccess::NewNote(heading_component);
    NotesWorkspaceActionsTestAccess::SeedHeadingDocument(heading_component);
    NotesWorkspaceActionsTestAccess::RenderWorkspace(heading_component);
    NotesWorkspaceActionsTestAccess::RenderWorkspace(heading_component);
    const ftxui::Box heading_column =
        NotesWorkspaceActionsTestAccess::EditorColumnBox(heading_component, 0);
    const ftxui::Screen heading_screen =
        NotesWorkspaceActionsTestAccess::RenderWorkspaceScreen(
            heading_component, 100, 28);
    const textlt::Theme& heading_theme = textlt::FallbackTheme();
    const ftxui::Cell& h1_cell = heading_screen.PixelAt(
        heading_column.x_min, heading_column.y_min);
    const ftxui::Cell& h2_cell = heading_screen.PixelAt(
        heading_column.x_min, heading_column.y_min + 1);
    const ftxui::Cell& h3_cell = heading_screen.PixelAt(
        heading_column.x_min, heading_column.y_min + 2);
    Expect(h1_cell.background_color == heading_theme.modal_selected_item_bg,
        "H1 should fill its row with the action-title background color.");
    Expect(h1_cell.foreground_color == heading_theme.modal_selected_item_fg,
        "H1 should use the action-title foreground color.");
    Expect(h1_cell.bold, "H1 text should be bold.");
    Expect(heading_screen.PixelAt(heading_column.x_max, heading_column.y_min)
                .background_color == heading_theme.modal_selected_item_bg,
        "H1 should fill the whole row, including the trailing blank cells.");
    Expect(h2_cell.foreground_color == heading_theme.syntax_keyword,
        "H2 should use the theme keyword accent color.");
    Expect(h2_cell.bold, "H2 text should be bold.");
    Expect(h3_cell.foreground_color == heading_theme.syntax_type,
        "H3 should use the theme type accent color.");
    Expect(h3_cell.bold, "H3 text should be bold.");

    const ftxui::Box h2_button =
        NotesWorkspaceActionsTestAccess::HeadingBox(heading_component, 2);
    Expect(h2_button.x_max >= h2_button.x_min &&
            h2_button.y_max >= h2_button.y_min,
        "The H2 toolbar button should have a visible hit box.");
    Expect(NotesWorkspaceActionsTestAccess::MouseAt(
            heading_component, ftxui::Mouse::Left, ftxui::Mouse::Pressed,
            h2_button.x_min + 1, h2_button.y_min),
        "Clicking the H2 toolbar button should be handled.");
    Expect(NotesWorkspaceActionsTestAccess::ActiveHeading(heading_component) == 2,
        "H2 should become the active heading level.");
    Expect(NotesWorkspaceActionsTestAccess::MouseAt(
            heading_component, ftxui::Mouse::Left, ftxui::Mouse::Pressed,
            h2_button.x_min + 1, h2_button.y_min),
        "Clicking the active H2 toolbar button should be handled.");
    Expect(NotesWorkspaceActionsTestAccess::ActiveHeading(heading_component) == 0,
        "Clicking the active heading again should return to a paragraph.");

    fs::remove_all(heading_root, cleanup_error);

    // The Copy toolbar button copies the paragraph under the cursor with
    // leading and trailing whitespace trimmed.
    const fs::path copy_root =
        fs::temp_directory_path() / "textlt_notes_workspace_copy_test";
    fs::remove_all(copy_root, cleanup_error);
    std::string copied_text;
    NotesWorkspaceComponent copy_component(
        nullptr, [](const std::string&) {}, [] {}, [] {},
        [&copied_text](const std::string& text) { copied_text = text; return true; },
        copy_root);
    NotesWorkspaceActionsTestAccess::NewNote(copy_component);
    NotesWorkspaceActionsTestAccess::SeedEditorDocument(copy_component, 1);
    NotesWorkspaceActionsTestAccess::SetFirstBlockText(
        copy_component, "   padded paragraph\t ");
    NotesWorkspaceActionsTestAccess::RenderWorkspace(copy_component);
    NotesWorkspaceActionsTestAccess::RenderWorkspace(copy_component);
    const ftxui::Box copy_button =
        NotesWorkspaceActionsTestAccess::CopyBox(copy_component);
    Expect(copy_button.x_max >= copy_button.x_min &&
            copy_button.y_max >= copy_button.y_min,
        "The Copy toolbar button should have a visible hit box.");
    Expect(NotesWorkspaceActionsTestAccess::MouseAt(
            copy_component, ftxui::Mouse::Left, ftxui::Mouse::Pressed,
            copy_button.x_min + 1, copy_button.y_min),
        "Clicking the Copy toolbar button should be handled.");
    Expect(copied_text == "padded paragraph",
        "Copy should place the cursor paragraph on the clipboard with trimmed spaces.");

    NotesWorkspaceActionsTestAccess::SetFirstBlockText(copy_component,
        "See [the article](https://example.com/wiki/Topic_(details) \"Article\").");
    Expect(NotesWorkspaceActionsTestAccess::MouseAt(
            copy_component, ftxui::Mouse::Left, ftxui::Mouse::Pressed,
            copy_button.x_min + 1, copy_button.y_min),
        "Clicking Copy on a Markdown link should be handled.");
    Expect(copied_text == "https://example.com/wiki/Topic_(details)",
        "Copy should extract the Markdown link address without its label or title.");

    fs::remove_all(copy_root, cleanup_error);

    // Cards in one overview row must have identical widths. Otherwise long
    // titles/previews make one card wider, overflow the row, and interrupt the
    // neighboring cards' vertical borders.
    const fs::path cards_root =
        fs::temp_directory_path() / "textlt_notes_workspace_cards_test";
    fs::remove_all(cards_root, cleanup_error);
    NotesWorkspaceComponent cards_component(
        nullptr, [](const std::string&) {}, [] {}, [] {},
        [](const std::string&) { return true; }, cards_root);
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
