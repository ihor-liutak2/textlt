#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

#include "file_manager.hpp"
#include "modals/modal_interface.hpp"
#include "modals/modal_window.hpp"
#include "remote/remote_config_store.hpp"
#include "remote/remote_entry.hpp"
#include "remote/remote_dropbox_provider.hpp"
#include "remote/remote_google_drive_provider.hpp"
#include "remote/remote_local_provider.hpp"
#include "remote/remote_microsoft_drive_provider.hpp"
#include "remote/remote_provider.hpp"
#include "remote/remote_sftp_provider.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt {

class RemoteFilesModalContent : public IModalContent {
public:
    using StartDirectoryProvider = std::function<std::filesystem::path()>;
    using OpenLocalFileCallback = std::function<bool(const std::filesystem::path&, std::string&)>;
    using CopyTextCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void()>;

    RemoteFilesModalContent(
        const Theme* theme,
        RemoteConfigStore* config_store,
        FileManager* file_manager,
        StartDirectoryProvider start_directory_provider,
        OpenLocalFileCallback open_local_file,
        CopyTextCallback copy_text,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Remote Files"; }
    ModalSizePreference GetModalSizePreference() const override { return {120, 32}; }
    std::string GetFooterText() const override { return status_; }
    bool HasCustomFooter() const override { return !error_footer_.empty(); }
    int GetCustomFooterHeight() const override;
    ftxui::Element RenderCustomFooter() override;

    void Open();
    void Close();

private:
    enum class PanelSide {
        Local,
        Remote,
    };

    enum class PendingOperation {
        None,
        Rename,
        MakeDirectory,
        Delete,
        CopyToRemoteOverwrite,
        CopyToLocalOverwrite,
    };

    struct PanelState {
        std::string title;
        std::string path;
        std::string path_input;
        int path_cursor = 0;
        std::vector<RemoteEntry> entries;
        std::vector<ftxui::Box> boxes;
        int selected = 0;
        int scroll_offset = 0;
        std::string status;
        bool status_is_error = false;
        int last_clicked_entry = -1;
        std::chrono::steady_clock::time_point last_click_time{};
    };

    ftxui::Component MakeTextButton(
        std::string label,
        std::function<void()> on_click,
        ButtonRole role = ButtonRole::Default,
        ButtonVariant variant = ButtonVariant::AccentBar,
        std::string icon = {},
        ButtonSize size = ButtonSize::Normal);
    ftxui::Component MakePathInput(PanelSide side);
    ftxui::Element RenderPanel(PanelSide side);
    ftxui::Element RenderEntryRow(PanelSide side, size_t index);
    ftxui::Element RenderOperationRow();
    bool HandlePanelEvent(PanelSide side, ftxui::Event event);
    bool HandleGlobalContentEvent(ftxui::Event event);
    int EntryIndexAtMouse(const PanelState& panel, const ftxui::Mouse& mouse) const;

    void ReloadConnections();
    bool EnsureRemoteProvider();
    void LoadPanel(PanelSide side, const std::string& path);
    void LoadLocalPathFromInput();
    void LoadRemotePathFromInput();
    void RefreshAll();
    void RefreshActive();
    void SelectPanel(PanelSide side);
    void SelectEntry(PanelSide side, int index);
    void OpenSelected(PanelSide side);
    void GoParent(PanelSide side);
    void CopyToRemote();
    void CopyToLocal();
    void CopyToRemoteConfirmed(const RemoteEntry& entry, const std::string& remote_target);
    void CopyToLocalConfirmed(const RemoteEntry& entry, const std::string& local_target);
    void StartOverwriteConfirmation(PendingOperation operation, const RemoteEntry& entry, const std::string& target_path);
    void CopyActiveToOtherPanel();
    void OpenSelectedFile();
    bool UploadCachedLocalFile(const std::filesystem::path& local_path, std::string& status, std::string& error);
    void UploadLastOpenedRemoteFile();
    void ClearCachedRemoteFiles();
    void CopySelectedPath();
    void NextConnection();
    void PreviousConnection();

    void StartRename();
    void StartMakeDirectory();
    void StartDelete();
    void ConfirmPendingOperation();
    void CancelPendingOperation();

    RemoteEntry* SelectedEntry(PanelSide side);
    const RemoteEntry* SelectedEntry(PanelSide side) const;
    PanelState& Panel(PanelSide side);
    const PanelState& Panel(PanelSide side) const;
    IRemoteProvider* Provider(PanelSide side);
    const IRemoteProvider* Provider(PanelSide side) const;

    static std::string EntryTypeLabel(RemoteEntryType type);
    static std::string EntrySizeLabel(const RemoteEntry& entry);
    static std::string TrimForDisplay(const std::string& value, size_t width);
    static bool IsParentEntry(const RemoteEntry& entry);
    bool PanelContainsName(PanelSide side, const std::string& name) const;
    static std::string LocalPathKey(const std::filesystem::path& path);
    std::filesystem::path TempDownloadPath(const std::string& name) const;
    void RememberCachedRemoteFile(
        const std::filesystem::path& local_path,
        const RemoteEntry& remote_entry,
        const RemoteConnectionConfig& config);
    std::string LastCachedLabel() const;
    void SetStatus(std::string status, bool is_error = false);
    void SetPanelStatus(PanelSide side, std::string status, bool is_error = false);

    const Theme* theme_ = nullptr;
    RemoteConfigStore* config_store_ = nullptr;
    FileManager* file_manager_ = nullptr;
    StartDirectoryProvider start_directory_provider_;
    OpenLocalFileCallback open_local_file_;
    CopyTextCallback copy_text_;
    CloseCallback on_close_;

    RemoteLocalProvider local_provider_;
    std::unique_ptr<IRemoteProvider> remote_provider_;
    std::vector<RemoteConnectionConfig> connections_;
    int selected_connection_ = 0;

    PanelState local_panel_;
    PanelState remote_panel_;
    PanelSide active_panel_ = PanelSide::Local;

    PendingOperation pending_operation_ = PendingOperation::None;
    PanelSide pending_panel_ = PanelSide::Local;
    std::string pending_input_label_;
    std::string pending_input_value_;
    int pending_input_cursor_ = 0;
    RemoteEntry pending_copy_entry_;
    std::string pending_copy_target_path_;

    struct CachedRemoteFile {
        std::filesystem::path local_path;
        std::string remote_path;
        std::string remote_name;
        RemoteConnectionConfig connection;
    };

    std::vector<CachedRemoteFile> cached_remote_files_;
    int last_cached_remote_index_ = -1;

    std::string status_ = "Ready.";
    bool status_is_error_ = false;
    std::string error_footer_;

    ftxui::Component prev_connection_button_;
    ftxui::Component next_connection_button_;
    ftxui::Component refresh_button_;
    ftxui::Component copy_to_remote_button_;
    ftxui::Component copy_to_local_button_;
    ftxui::Component open_button_;
    ftxui::Component sync_opened_button_;
    ftxui::Component clear_cache_button_;
    ftxui::Component copy_path_button_;
    ftxui::Component mkdir_button_;
    ftxui::Component rename_button_;
    ftxui::Component delete_button_;
    ftxui::Component close_button_;
    ftxui::Component copy_error_button_;
    ftxui::Component local_path_input_;
    ftxui::Component remote_path_input_;
    ftxui::Component local_list_component_;
    ftxui::Component remote_list_component_;
    ftxui::Component operation_input_;
    ftxui::Component confirm_button_;
    ftxui::Component cancel_button_;
    ftxui::Component container_inner_;
    ftxui::Component container_;
};

class RemoteFilesModal {
public:
    using StartDirectoryProvider = RemoteFilesModalContent::StartDirectoryProvider;
    using OpenLocalFileCallback = RemoteFilesModalContent::OpenLocalFileCallback;
    using CopyTextCallback = RemoteFilesModalContent::CopyTextCallback;

    RemoteFilesModal(
        const Theme* theme,
        RemoteConfigStore* config_store,
        FileManager* file_manager,
        StartDirectoryProvider start_directory_provider,
        OpenLocalFileCallback open_local_file,
        CopyTextCallback copy_text);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<RemoteFilesModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
