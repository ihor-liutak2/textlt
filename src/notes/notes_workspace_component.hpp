#pragma once

#include "workspace_tabs.hpp"

#include "editor/clipboard_controller.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "notes/note_repository.hpp"
#include "notes/note_session.hpp"
#include "notes/notes_context_menu.hpp"
#include "theme.hpp"

namespace textlt::notes {

struct ProjectContext {
    std::string id;
    std::string name;
};

class NotesWorkspaceComponent : public ftxui::ComponentBase {
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using DocumentsCallback = std::function<void()>;
    using ActionsCallback = std::function<void()>;
    using ClipboardWriteCallback = ClipboardController::WriteCallback;
    NotesWorkspaceComponent(
        const Theme* theme,
        StatusCallback on_status,
        DocumentsCallback on_documents,
        ActionsCallback on_actions,
        ClipboardWriteCallback write_clipboard,
        std::filesystem::path root = {});
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }
    void Open();
    // Updates the active project and the set of projects that still exist.
    // The project group is created lazily and is never associated by name.
    void SetProjectContext(
        std::optional<ProjectContext> project,
        std::vector<std::string> known_project_ids,
        bool project_registry_usable = true);
    bool UpdateProjectGroupName(const ProjectContext& project, std::string& error);
    bool RelinkGroupForNewProject(const ProjectContext& project, std::string& error);
    bool DetachProjectGroup(const std::string& project_id, std::string& error);
    bool Save(std::string& error);
    bool IsEditing() const { return editing_; }
    bool HasOpenContextMenu() const { return context_menu_.IsOpen(); }
    bool HandleContextMenuEvent(ftxui::Event event);
    void SetConnectCallback(std::function<void()> callback) {
        workspace_tabs_.SetConnectCallback(std::move(callback));
    }
    void SetClipboardReader(ClipboardController::ReadCallback reader) {
        read_clipboard_ = std::move(reader);
    }
    bool IsTrashSelected() const { return trash_selected_ && !editing_; }
    void SetSidebarVisible(bool visible);
    bool IsSidebarVisible() const { return sidebar_visible_; }
    void SetEditorColumns(int columns);
    int EditorColumns() const { return editor_columns_; }
    void SetColumnsChangedCallback(std::function<void(int)> callback) {
        on_columns_changed_ = std::move(callback);
    }
    void FocusSidebar();
    void FocusMainArea();
    std::string StatusText() const;
    void ToggleHeading(int level);
    void SetBlockType(NoteBlockType type);
    void CopyCurrentParagraph();
    struct TextTarget {
        std::string note_id;
        uint64_t revision;
        NotePosition start;
        NotePosition end;
        std::string text;
    };
    std::optional<TextTarget> CaptureTextTarget() const;
    bool ReplaceTextTarget(const TextTarget& target, const std::string& replacement, std::string& error);
    void NewNote();
    bool ImportNote(std::string title, std::vector<NoteBlock> blocks, std::string& error);
    bool ImportNoteTo(std::string title, std::vector<NoteBlock> blocks,
                      const std::string& section_id, std::string& error);
    const NoteDocument* SelectedNote() const;
    const std::vector<NoteSection>& Sections() const { return repository_.Sections(); }
    // Opens the card selected in the overview. Does nothing (with a status
    // message) when there is no editable note selection.
    void EditSelectedNote();
    // Trash-aware delete used by the Notes menu: trashed notes are deleted
    // permanently, everything else is moved to Trash.
    void DeleteSelection();

    // Structured note-body search used by the shared Find/Replace sidebar.
    struct NoteSearchResult {
        size_t block = 0;
        size_t byte = 0;
        size_t length = 0;
        std::string preview;
        size_t preview_match_start = 0;
        size_t preview_match_length = 0;
    };
    uint64_t NoteRevision() const;
    const NoteDocument* EditingNote() const { return editing_ ? session_.Note() : nullptr; }
    std::vector<NoteSearchResult> SearchNote(const std::string& query,
        bool match_case, bool whole_word, size_t limit, bool& truncated);
    void ActivateNoteSearchResult(size_t index);
    bool ReplaceNoteSearchResult(size_t index, const std::string& replacement,
        std::string& error);
    bool ReplaceAllNoteText(const std::string& query,
        const std::string& replacement, bool match_case, bool whole_word,
        size_t& count, std::string& error);
    void ClearNoteSearchHighlights();

private:
    friend class NotesWorkspaceActionsTestAccess;

    enum class FocusArea { Sidebar, Search, Cards, Body, GroupName };
    enum class SidebarEntryKind { Home, Section, NewSection, Trash };
    enum class SortMode { Updated, Created, Title, Pinned };
    enum class ConfirmAction { None, TrashNote, DeleteGroup, DeletePermanently, EmptyTrash };
    struct SidebarEntry {
        SidebarEntryKind kind;
        std::string id;
        std::string label;
        size_t count = 0;
        bool project = false;
        bool orphaned = false;
    };

    // One rendered row of the note document. A block may occupy several rows
    // when its text has to wrap, so the editor scrolls over visual rows rather
    // than over model blocks.
    struct EditorRow {
        size_t block = 0;
        size_t start = 0;
        size_t end = 0;
        std::string prefix;
        bool last = true;
    };

    void RebuildSidebar();
    void RebuildVisibleNotes();
    bool NoteMatchesFilter(const NoteDocument& note) const;
    void SelectSidebarEntry(int index);
    bool SelectSectionById(const std::string& id);
    bool IsKnownProject(const std::string& id) const;
    void OpenSelectedNote();
    void OpenNote(size_t note_index);
    void CloseEditor();
    void StartCreateSection();
    void FinishCreateSection();
    void StartRenameSection();
    void RequestDeleteSelectedSection();
    void DeleteSelectedSection();
    void DeleteSelectedNote();
    void RestoreSelectedNote();
    void PermanentlyDeleteSelectedNote();
    void EmptyTrash();
    void OpenProjectNotes();
    bool ConvertSelectedGroupToGeneral();
    void RelinkSelectedGroupToActiveProject();
    void RequestConfirmation(ConfirmAction action, std::string title, std::string message);
    void AcceptConfirmation();
    void CloseConfirmation();
    void OpenGroupPicker();
    void SelectGroupFromPicker(size_t index);
    void CycleSortMode();
    void TogglePin();
    void MoveActiveNoteToNextSection();
    void DeleteOrRestoreActiveNote();
    void TouchAndSave();
    void Notify(const std::string& message);
    bool HandleOverviewEvent(ftxui::Event event);
    bool HandleEditorEvent(ftxui::Event event);
    void ExecuteContextCommand(NotesContextMenu::Command command);
    void CopySelectionToClipboard(bool cut);
    void PasteText(const std::string& text);
    void PasteFromClipboard();
    void EditHistory(bool redo);
    bool HandleTextFieldEvent(std::string& value, size_t& cursor, ftxui::Event event, std::function<void()> on_change);
    ftxui::Element RenderSidebar();
    ftxui::Element RenderSidebarContents(const Theme& theme);
    ftxui::Element RenderOverview();
    ftxui::Element RenderEditor();
    ftxui::Element RenderConfirmationPopup();
    ftxui::Element RenderGroupPicker();
    void OpenTitleEditor();
    void CloseTitleEditor();
    void CommitTitleEditor();
    void LimitTitleEditorInput();
    ftxui::Element RenderTitleEditor();
    ftxui::Element RenderCard(NoteDocument& note, bool selected,
        size_t visible_index, int card_width);
    ftxui::Element RenderEditorRow(const EditorRow& row);
    ftxui::Element RenderEditorScrollbar(size_t viewport_rows, size_t page_rows,
        size_t total_rows, size_t scroll, const Theme& theme);
    void BuildEditorRows(size_t width);
    std::optional<size_t> EditorRowForPosition(const NotePosition& position) const;
    std::optional<NotePosition> NotePositionAt(int x, int y) const;
    void ScrollEditor(int delta);
    void EnsureEditorCursorVisible(size_t page_rows);
    void JumpToNotePosition(NotePosition position);
    void ResetEditorViewport();
    ftxui::Element ToolbarItem(const std::string& text, bool active, ftxui::Box& box);
    std::string SectionName(const NoteDocument& note) const;
    std::optional<size_t> ActiveNoteIndex() const;

    const Theme* theme_ = nullptr;
    StatusCallback on_status_;
    WorkspaceTabs workspace_tabs_;
    ClipboardWriteCallback write_clipboard_;
    ClipboardController::ReadCallback read_clipboard_;
    NoteRepository repository_;
    NoteSession session_;
    NotesContextMenu context_menu_;
    std::vector<SidebarEntry> sidebar_entries_;
    std::vector<size_t> visible_notes_;
    std::vector<NoteSearchResult> note_search_matches_;
    size_t note_search_active_ = 0;
    std::optional<ProjectContext> project_context_;
    std::vector<std::string> known_project_ids_;
    bool project_registry_usable_ = true;
    std::optional<std::string> active_project_section_id_;
    std::string selected_section_id_;
    bool trash_selected_ = false;
    SortMode sort_mode_ = SortMode::Updated;
    bool editing_ = false;
    bool sidebar_visible_ = true;
    bool sidebar_content_selected_ = false;
    size_t heading_scroll_ = 0;
    std::vector<size_t> heading_blocks_;
    std::vector<ftxui::Box> heading_boxes_;
    int last_heading_height_ = 0;
    int selected_sidebar_ = 0;
    int selected_card_ = 0;
    std::optional<std::string> active_note_id_;
    FocusArea focus_ = FocusArea::Cards;
    std::vector<EditorRow> editor_rows_;
    std::vector<ftxui::Box> editor_column_boxes_;
    int editor_columns_ = 1;
    int editor_scroll_ = 0;
    size_t editor_visible_rows_ = 0;
    size_t editor_column_rows_ = 0;
    size_t editor_content_width_ = 0;
    bool editor_view_known_ = false;
    bool editor_scrollbar_dragging_ = false;
    bool editor_mouse_selecting_ = false;
    int editor_scrollbar_drag_y_ = 0;
    int editor_scrollbar_drag_scroll_ = 0;
    NotePosition last_editor_cursor_{};
    bool has_last_editor_cursor_ = false;
    std::string search_;
    size_t search_cursor_ = 0;
    std::string group_name_input_;
    std::optional<std::string> editing_section_id_;
    size_t group_name_cursor_ = 0;
    std::string load_warning_;
    std::string save_status_ = "Ready";
    std::chrono::steady_clock::time_point last_saved_{};
    ConfirmAction confirm_action_ = ConfirmAction::None;
    std::string confirm_title_;
    std::string confirm_message_;
    bool group_picker_open_ = false;
    bool title_editor_open_ = false;
    std::string title_editor_input_;
    size_t title_editor_cursor_ = 0;
    ftxui::Box sidebar_box_;
    ftxui::Box sidebar_notes_tab_box_;
    ftxui::Box sidebar_content_tab_box_;
    ftxui::Box sidebar_content_box_;
    ftxui::Box footer_new_note_box_;
    ftxui::Box footer_delete_note_box_;
    ftxui::Box footer_restore_note_box_;
    ftxui::Box footer_permanent_delete_box_;
    ftxui::Box footer_empty_trash_box_;
    ftxui::Box footer_new_group_box_;
    ftxui::Box footer_rename_group_box_;
    ftxui::Box footer_delete_group_box_;
    ftxui::Box footer_project_notes_box_;
    ftxui::Box footer_orphan_general_box_;
    ftxui::Box footer_orphan_relink_box_;
    ftxui::Box confirm_yes_box_;
    ftxui::Box confirm_cancel_box_;
    std::vector<ftxui::Box> group_picker_boxes_;
    ftxui::Box group_picker_cancel_box_;
    ftxui::Box title_editor_save_box_;
    ftxui::Box title_editor_cancel_box_;
    ftxui::Box sort_box_;
    ftxui::Box search_box_;
    ftxui::Box overview_box_;
    ftxui::Box editor_view_box_;
    ftxui::Box editor_scrollbar_box_;
    ftxui::Box new_note_box_;
    ftxui::Box back_box_;
    ftxui::Box title_box_;
    ftxui::Box pin_box_;
    ftxui::Box section_box_;
    ftxui::Box trash_box_;
    ftxui::Box heading_1_box_;
    ftxui::Box heading_2_box_;
    ftxui::Box heading_3_box_;
    ftxui::Box copy_box_;
    ftxui::Box col_1_box_;
    ftxui::Box col_2_box_;
    ftxui::Box col_3_box_;
    std::vector<ftxui::Box> sidebar_row_boxes_;
    std::vector<ftxui::Box> card_boxes_;
    std::function<void(int)> on_columns_changed_;
};

} // namespace textlt::notes
