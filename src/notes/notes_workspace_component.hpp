#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "notes/note_repository.hpp"
#include "notes/note_session.hpp"
#include "theme.hpp"

namespace textlt::notes {

class NotesWorkspaceComponent : public ftxui::ComponentBase {
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using DocumentsCallback = std::function<void()>;
    using ClipboardReadCallback = std::function<std::string()>;
    using ClipboardWriteCallback = std::function<void(const std::string&)>;
    using CanSyncCallback = std::function<bool()>;
    using SyncProgressCallback = std::function<void(const std::string&)>;
    using SyncCallback = std::function<bool(
        const std::filesystem::path&,
        SyncProgressCallback,
        std::string&)>;
    using RequestRedrawCallback = std::function<void()>;
    NotesWorkspaceComponent(
        const Theme* theme,
        StatusCallback on_status,
        DocumentsCallback on_documents,
        ClipboardReadCallback read_clipboard,
        ClipboardWriteCallback write_clipboard,
        CanSyncCallback can_sync,
        SyncCallback sync,
        RequestRedrawCallback request_redraw,
        std::filesystem::path root = {});
    ~NotesWorkspaceComponent() override;
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }
    void Open();
    bool Save(std::string& error);
    bool IsEditing() const { return editing_; }
    std::string StatusText() const;
    void ToggleMark(NoteMark mark);
    void SetBlockType(NoteBlockType type);
    void ClearFormatting();
    void NewNote();

private:
    friend class NotesWorkspaceSyncTestAccess;

    enum class FocusArea { Sidebar, Search, Cards, Title, Body, GroupName };
    enum class SidebarEntryKind { Home, Section, NewSection, Trash };
    struct SidebarEntry { SidebarEntryKind kind; std::string id; std::string label; size_t count = 0; };

    void RebuildSidebar();
    void RebuildVisibleNotes();
    bool NoteMatchesFilter(const NoteDocument& note) const;
    void SelectSidebarEntry(int index);
    void OpenSelectedNote();
    void OpenNote(size_t note_index);
    void CloseEditor();
    void StartCreateSection();
    void FinishCreateSection();
    void StartRenameSection();
    void DeleteSelectedSection();
    void TogglePin();
    void MoveActiveNoteToNextSection();
    void DeleteOrRestoreActiveNote();
    void TouchAndSave();
    void Notify(const std::string& message);
    void RunSync();
    void ApplySyncCompletion();
    void CloseSyncPopup();
    void RequestSyncRedraw() noexcept;
    void CompleteSyncWorker(bool success, std::string error) noexcept;
    bool HandleOverviewEvent(ftxui::Event event);
    bool HandleEditorEvent(ftxui::Event event);
    bool HandleTextFieldEvent(std::string& value, size_t& cursor, ftxui::Event event, std::function<void()> on_change);
    ftxui::Element RenderSidebar();
    ftxui::Element RenderOverview();
    ftxui::Element RenderEditor();
    ftxui::Element RenderSyncPopup();
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
    CanSyncCallback can_sync_;
    SyncCallback sync_;
    RequestRedrawCallback request_redraw_;
    NoteRepository repository_;
    NoteSession session_;
    std::vector<SidebarEntry> sidebar_entries_;
    std::vector<size_t> visible_notes_;
    std::string selected_section_id_;
    bool trash_selected_ = false;
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
    std::thread sync_thread_;
    mutable std::mutex sync_mutex_;
    std::atomic<bool> sync_running_{false};
    std::atomic<int> sync_frame_{0};
    bool sync_completed_ = false;
    bool sync_success_ = false;
    bool sync_popup_open_ = false;
    std::string sync_popup_message_;
    std::string sync_error_;
    ftxui::Box sidebar_box_;
    ftxui::Box documents_tab_box_;
    ftxui::Box sync_box_;
    ftxui::Box sync_popup_close_box_;
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
