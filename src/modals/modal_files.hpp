#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

#include "file_manager.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

enum class FilesModalMode {
    None,
    Open,
    SaveAs,
    Import,
    Export,
    Manage,
};

class FilesModalContent : public IModalContent {
public:
    using StartDirectoryProvider = std::function<std::filesystem::path()>;
    using FavoriteDirectoriesProvider = std::function<std::vector<std::filesystem::path>()>;
    using AddFavoriteDirectoryCallback = std::function<bool(
        const std::filesystem::path& directory,
        std::string& error)>;
    using CopyPathCallback = std::function<void(const std::string& text)>;
    using ConfirmPathCallback = std::function<bool(
        FilesModalMode mode,
        const std::filesystem::path& path,
        std::string& error)>;
    using CloseCallback = std::function<void()>;

    FilesModalContent(
        const Theme* theme,
        FileManager* file_manager,
        StartDirectoryProvider start_directory_provider,
        FavoriteDirectoriesProvider favorite_directories_provider,
        AddFavoriteDirectoryCallback on_add_favorite_directory,
        CopyPathCallback on_copy_path,
        ConfirmPathCallback on_confirm_path,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override;
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {112, 34}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return {}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open(FilesModalMode mode,
              const std::filesystem::path& start_path,
              std::string suggested_file_name);
    void Close();

    FilesModalMode Mode() const { return mode_; }
    void Refresh();
    void ConfirmSelected();
    void NavigateUp();
    bool HandleEvent(ftxui::Event event);

private:
    enum class PendingFileOperation {
        None,
        CreateDirectory,
        CreateFile,
        DeleteItems,
        RenameItem,
        PasteItems,
    };

    ftxui::Component MakeTextButton(
        std::string label,
        std::function<void()> on_click);

    void RebuildComponents();
    void RefreshFavorites();
    void LoadDirectory(const std::filesystem::path& directory);
    void LoadPathFromInput();
    void LoadBuiltInDirectory(const std::filesystem::path& directory);
    void AddCurrentDirectoryToFavorites();
    void CopySelectedPathText();
    void StartCreateDirectoryOperation();
    void StartCreateFileOperation();
    void StartDeleteOperation();
    void StartRenameOperation();
    void StartCopyOperation();
    void StartCutOperation();
    void StartPasteOperation();
    void StartNameOperation(
        PendingFileOperation operation,
        std::string label,
        std::string default_value,
        std::string message);
    void StartConfirmOperation(PendingFileOperation operation, std::string message);
    void CancelPendingOperation();
    void ConfirmPendingOperation();
    bool HasPendingOperation() const;
    bool PendingOperationNeedsInput() const;
    std::string PendingOperationActionLabel() const;
    std::vector<std::filesystem::path> SelectedOperationPaths(std::string& error) const;
    std::vector<int> SortedSelectedIndices() const;
    void ClearSelectionMarks();
    void ToggleEntrySelection(int index);
    void SelectRangeTo(int index);
    bool IsEntryMarkedSelected(int index) const;
    void MoveSelectionWithRange(int delta);
    void ActivateSelected(bool double_click);
    void SelectEntry(int index);
    void MoveSelection(int delta);
    void ClampSelection();
    void EnsureSelectionVisible();
    void SetStatus(std::string message, bool error = false);
    bool SelectedPath(std::filesystem::path& path, std::string& error) const;
    bool TargetPathForMode(std::filesystem::path& path, std::string& error) const;
    FileFilter FilterForMode() const;
    bool IsParentEntry(const FileEntry& entry) const;
    bool IsFileActionMode() const;
    bool IsSaveLikeMode() const;
    bool HandleEntryMouseEvent(ftxui::Event event);
    bool HandleFavoriteMouseEvent(ftxui::Event event);
    bool IsBackspaceEvent(const ftxui::Event& event) const;
    bool IsEscapeEvent(const ftxui::Event& event) const;
    bool IsShiftArrowUpEvent(const ftxui::Event& event) const;
    bool IsShiftArrowDownEvent(const ftxui::Event& event) const;

    ftxui::Element RenderTopButtons();
    ftxui::Element RenderFavoriteDirectories();
    ftxui::Element RenderPathInput();
    ftxui::Element RenderOperationButtons();
    ftxui::Element RenderEntryList();
    ftxui::Element RenderFileNameInput();
    ftxui::Element RenderSelectionSummary() const;
    ftxui::Element RenderStatusSummary() const;
    ftxui::Element RenderConfirmRow();

    std::string ModeTitle() const;
    std::string FooterActionLabel() const;
    std::string FormatCurrentDirectory() const;
    std::string FormatEntryName(const FileEntry& entry, size_t width) const;
    std::string FormatSize(const FileEntry& entry) const;
    std::string TrimForDisplay(const std::string& text, size_t max_size) const;
    std::string SuggestedFileNameFromPath(const std::filesystem::path& path) const;

    const Theme* theme_ = nullptr;
    FileManager* file_manager_ = nullptr;
    StartDirectoryProvider start_directory_provider_;
    FavoriteDirectoriesProvider favorite_directories_provider_;
    AddFavoriteDirectoryCallback on_add_favorite_directory_;
    CopyPathCallback on_copy_path_;
    ConfirmPathCallback on_confirm_path_;
    CloseCallback on_close_;

    FilesModalMode mode_ = FilesModalMode::None;
    std::filesystem::path current_directory_;
    std::vector<FileEntry> entries_;
    std::vector<std::filesystem::path> favorite_directories_;
    std::vector<ftxui::Box> entry_boxes_;
    std::vector<ftxui::Box> favorite_boxes_;
    int selected_entry_ = 0;
    int scroll_offset_ = 0;
    std::set<int> selected_indices_;
    int selection_anchor_ = -1;
    std::string path_input_value_;
    int path_input_cursor_ = 0;
    std::string file_name_input_value_;
    int file_name_input_cursor_ = 0;
    std::string status_ = "Ready.";
    bool status_is_error_ = false;
    PendingFileOperation pending_operation_ = PendingFileOperation::None;
    std::string pending_operation_message_;
    std::string pending_operation_input_label_;
    std::string pending_operation_input_value_;
    int pending_operation_input_cursor_ = 0;
    std::vector<std::filesystem::path> pending_operation_paths_;

    std::chrono::steady_clock::time_point last_entry_click_time_{};
    int last_clicked_entry_ = -1;

    ftxui::Component home_button_;
    ftxui::Component documents_button_;
    ftxui::Component downloads_button_;
    ftxui::Component current_dir_button_;
    ftxui::Component add_dir_button_;
    ftxui::Component refresh_button_;
    ftxui::Component copy_path_button_;
    ftxui::Component create_dir_button_;
    ftxui::Component create_file_button_;
    ftxui::Component delete_button_;
    ftxui::Component rename_button_;
    ftxui::Component copy_button_;
    ftxui::Component cut_button_;
    ftxui::Component paste_button_;
    ftxui::Component path_input_;
    ftxui::Component file_name_input_;
    ftxui::Component operation_input_;
    ftxui::Component confirm_yes_button_;
    ftxui::Component confirm_cancel_button_;
    ftxui::Component entry_list_component_;
    ftxui::Component container_;
};

class FilesModal {
public:
    using StartDirectoryProvider = FilesModalContent::StartDirectoryProvider;
    using FavoriteDirectoriesProvider = FilesModalContent::FavoriteDirectoriesProvider;
    using AddFavoriteDirectoryCallback = FilesModalContent::AddFavoriteDirectoryCallback;
    using CopyPathCallback = FilesModalContent::CopyPathCallback;
    using ConfirmPathCallback = FilesModalContent::ConfirmPathCallback;

    FilesModal(
        const Theme* theme,
        FileManager* file_manager,
        StartDirectoryProvider start_directory_provider,
        FavoriteDirectoriesProvider favorite_directories_provider,
        AddFavoriteDirectoryCallback on_add_favorite_directory,
        CopyPathCallback on_copy_path,
        ConfirmPathCallback on_confirm_path);

    ftxui::Component View() const;

    void Open(FilesModalMode mode,
              const std::filesystem::path& start_path,
              std::string suggested_file_name = {});
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    void RebuildFooterButtons();

    bool open_ = false;
    const Theme* theme_ = nullptr;

    std::shared_ptr<FilesModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
