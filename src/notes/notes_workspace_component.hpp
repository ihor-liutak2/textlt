#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "notes/note_repository.hpp"
#include "notes/note_session.hpp"
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
    using ClipboardReadCallback = std::function<std::string()>;
    using ClipboardWriteCallback = std::function<void(const std::string&)>;
    NotesWorkspaceComponent(
        const Theme* theme,
        StatusCallback on_status,
        DocumentsCallback on_documents,
        ClipboardReadCallback read_clipboard,
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
        std::vector<std::string> known_project_ids);
    bool Save(std::string& error);
    bool IsEditing() const { return editing_; }
    std::string StatusText() const;
    void ToggleMark(NoteMark mark);
    void SetBlockType(NoteBlockType type);
    void ClearFormatting();
    void NewNote();

private:
    friend class NotesWorkspaceActionsTestAccess;

    enum class FocusArea { Sidebar, Search, Cards, Title, Body, GroupName };
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
    void DeleteSelectedSection();
    void DeleteSelectedNote();
    void RestoreSelectedNote();
    void PermanentlyDeleteSelectedNote();
    void EmptyTrash();
    void OpenProjectNotes();
    void ConvertSelectedGroupToGeneral();
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
    bool HandleTextFieldEvent(std::string& value, size_t& cursor, ftxui::Event event, std::function<void()> on_change);
    ftxui::Element RenderSidebar();
    ftxui::Element RenderOverview();
    ftxui::Element RenderEditor();
    ftxui::Element RenderConfirmationPopup();
    ftxui::Element RenderGroupPicker();
    ftxui::Element RenderCard(NoteDocument& note, bool selected, size_t visible_index);
    ftxui::Element RenderRichBlock(const NoteBlock& block, size_t block_index, size_t number);
    ftxui::Element ToolbarItem(const std::string& text, bool active, ftxui::Box& box);
    std::string SectionName(const NoteDocument& note) const;
    std::optional<size_t> ActiveNoteIndex() const;

    const Theme* theme_ = nullptr;
    StatusCallback on_status_;
    DocumentsCallback on_documents_;
    ClipboardReadCallback read_clipboard_;
    ClipboardWriteCallback write_clipboard_;
    NoteRepository repository_;
    NoteSession session_;
    std::vector<SidebarEntry> sidebar_entries_;
    std::vector<size_t> visible_notes_;
    std::optional<ProjectContext> project_context_;
    std::vector<std::string> known_project_ids_;
    std::optional<std::string> active_project_section_id_;
    std::string selected_section_id_;
    bool trash_selected_ = false;
    SortMode sort_mode_ = SortMode::Updated;
    bool editing_ = false;
    int selected_sidebar_ = 0;
    int selected_card_ = 0;
    std::optional<std::string> active_note_id_;
    FocusArea focus_ = FocusArea::Cards;
    std::string search_;
    size_t search_cursor_ = 0;
    size_t title_cursor_ = 0;
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
    ftxui::Box sidebar_box_;
    ftxui::Box documents_tab_box_;
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
    ftxui::Box sort_box_;
    ftxui::Box search_box_;
    ftxui::Box overview_box_;
    ftxui::Box new_note_box_;
    ftxui::Box back_box_;
    ftxui::Box pin_box_;
    ftxui::Box section_box_;
    ftxui::Box trash_box_;
    ftxui::Box bold_box_;
    ftxui::Box italic_box_;
    ftxui::Box underline_box_;
    ftxui::Box strike_box_;
    ftxui::Box bullet_box_;
    ftxui::Box numbered_box_;
    ftxui::Box check_box_;
    ftxui::Box paragraph_box_;
    std::vector<ftxui::Box> sidebar_row_boxes_;
    std::vector<ftxui::Box> card_boxes_;
};

} // namespace textlt::notes
