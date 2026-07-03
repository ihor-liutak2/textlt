#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "git_manager.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class GitModalContent : public IModalContent {
public:
    using OpenFileCallback = std::function<bool(const std::filesystem::path&, std::string&)>;
    using OpenCompareCallback = std::function<bool(
        const std::string& left_title,
        const std::string& left_content,
        const std::string& right_title,
        const std::string& right_content,
        std::string& error)>;
    using WriteClipboardCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void()>;
    using RequestRedrawCallback = std::function<void()>;

    GitModalContent(
        const Theme* theme,
        GitManager* git_manager,
        OpenFileCallback on_open_file,
        OpenCompareCallback on_open_compare,
        WriteClipboardCallback write_clipboard,
        CloseCallback on_close,
        RequestRedrawCallback request_redraw);
    ~GitModalContent() override;

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Git"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {118, 34}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override;
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override { return 3; }
    ftxui::Element RenderCustomFooter() override;

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();
    bool HandleEvent(ftxui::Event event);

private:
    enum class Tab {
        Status = 0,
        Diff = 1,
        Commit = 2,
        Branches = 3,
        Remote = 4,
        Tags = 5,
        Server = 6,
        Compare = 7,
    };

    ftxui::Component MakeTextButton(std::string label, std::function<void()> on_click);
    ftxui::Component MakeTabButton(std::string label, int tab_index);

    void RefreshStatus();
    void RefreshBranches();
    void RefreshDiff();
    void RefreshCommitFiles();
    void RefreshCompareRefs();
    void RefreshCompareFiles();
    void RefreshAll();

    void ToggleSelectedFileStaged();
    void StageAllShownFiles();
    void UnstageAllShownFiles();
    void OpenSelectedFile();
    void CopyStatusPaths();

    void CopyDiff();
    void CommitStagedFiles();
    void OpenCompareSideBySide();
    void OpenCompareUnifiedDiff();
    void CopyCompareDiff();

    void CheckoutSelectedBranch();
    void MergeSelectedBranch();
    void RenameSelectedBranch();
    void UpdateSelectedBranch();
    void RebaseSelectedBranch();
    void DeleteSelectedBranch();

    void RefreshRemoteBranches();
    void CheckoutSelectedRemoteBranch();
    void DeleteSelectedRemoteBranch();

    void RefreshTags();
    void CreateTag();
    void DeleteSelectedTag();
    void PushSelectedTag();
    void PushAllTags();
    void FetchTags();

    void CheckConnection();
    void FetchServer();
    void PushServer();
    void ForcePushWithLease();

    void RequestConfirm(
        const std::string& title,
        const std::string& message,
        const std::string& command_preview,
        std::function<void()> on_confirm,
        const std::string& required_text = "");
    void ConfirmPendingAction();
    void CancelPendingAction();
    bool HandleConfirmEvent(ftxui::Event event);
    ftxui::Element RenderConfirmOverlay();

    enum class BackgroundOperation { None, Commit, CheckConnection, Fetch, Push, ForcePush };
    void StartBackgroundOperation(
        BackgroundOperation operation,
        std::string action,
        std::function<GitManager::CommandResult()> command);
    void ApplyBackgroundOperationCompletion();
    ftxui::Element RenderOperationOverlay() const;

    void RunAndRefresh(const std::string& action, const GitManager::CommandResult& result);
    bool HasUncommittedChanges() const;
    bool HasStagedFiles() const;
    std::string SelectedFilePath() const;
    std::string SelectedBranchName() const;
    std::string SelectedCompareFilePath() const;
    std::string SelectedCompareLeftRef() const;
    std::string SelectedCompareRightRef() const;
    std::filesystem::path RepositoryPath() const;

    void RebuildStatusLabels();
    void RebuildBranchLabels();
    void RebuildRemoteBranchLabels();
    void RebuildTagLabels();
    void RebuildCommitLabels();
    void RebuildCompareLabels();
    void SplitOutputLines(const std::string& output, std::vector<std::string>* lines) const;
    std::string TrimForDisplay(const std::string& text, size_t max_size) const;
    std::string StatusCodeForEntry(const GitManager::StatusEntry& entry) const;

    ftxui::Element RenderStatusTab();
    ftxui::Element RenderDiffTab();
    ftxui::Element RenderCommitTab();
    ftxui::Element RenderBranchesTab();
    ftxui::Element RenderRemoteTab();
    ftxui::Element RenderTagsTab();
    ftxui::Element RenderServerTab();
    ftxui::Element RenderCompareTab();
    ftxui::Element RenderTextLines(
        const std::vector<std::string>& lines,
        const std::string& empty_text,
        int scroll_offset = 0) const;

    const Theme* theme_ = nullptr;
    GitManager* git_manager_ = nullptr;
    OpenFileCallback on_open_file_;
    OpenCompareCallback on_open_compare_;
    WriteClipboardCallback write_clipboard_;
    CloseCallback on_close_;
    RequestRedrawCallback request_redraw_;

    std::thread operation_thread_;
    mutable std::mutex operation_mutex_;
    std::atomic<bool> operation_running_{false};
    bool operation_completed_ = false;
    std::atomic<int> operation_frame_{0};
    BackgroundOperation background_operation_ = BackgroundOperation::None;
    std::string operation_action_;
    GitManager::CommandResult operation_result_;

    int selected_tab_ = 0;
    std::string status_ = "Ready";
    std::string branch_;
    std::filesystem::path repo_root_;

    std::vector<GitManager::StatusEntry> status_entries_;
    std::vector<std::string> status_labels_;
    int selected_status_ = 0;

    std::string diff_text_;
    std::vector<std::string> diff_lines_;
    bool diff_staged_ = false;
    int diff_scroll_offset_ = 0;

    std::vector<std::string> commit_files_;
    std::vector<std::string> commit_labels_;
    int selected_commit_file_ = 0;
    std::string commit_message_;

    std::vector<std::string> branches_;
    std::vector<std::string> branch_labels_;
    int selected_branch_ = 0;
    std::string rename_branch_input_;

    std::vector<std::string> remote_branches_;
    std::vector<std::string> filtered_remote_branches_;
    std::vector<std::string> remote_branch_labels_;
    int selected_remote_branch_ = 0;
    std::string remote_branch_filter_;

    std::vector<std::string> tags_;
    std::vector<std::string> filtered_tags_;
    std::vector<std::string> tag_labels_;
    int selected_tag_ = 0;
    std::string tag_filter_;
    std::string tag_name_input_;
    std::string tag_message_input_;

    std::string server_output_;
    std::vector<std::string> server_output_lines_;
    int server_output_scroll_offset_ = 0;

    std::vector<GitManager::CompareRef> compare_refs_;
    std::vector<std::string> compare_ref_labels_;
    std::vector<GitManager::CompareEntry> compare_entries_;
    std::vector<std::string> compare_file_labels_;
    int selected_compare_left_ref_ = 1;
    int selected_compare_right_ref_ = 0;
    int selected_compare_file_ = 0;
    std::string compare_diff_text_;
    std::vector<std::string> compare_diff_lines_;
    int compare_diff_scroll_offset_ = 0;

    bool confirm_active_ = false;
    int confirm_layer_index_ = 0;
    std::string confirm_title_;
    std::string confirm_message_;
    std::string confirm_command_preview_;
    std::string confirm_required_text_;
    std::string confirm_typed_text_;
    std::function<void()> confirm_action_;

    ftxui::Component status_tab_button_;
    ftxui::Component diff_tab_button_;
    ftxui::Component commit_tab_button_;
    ftxui::Component branches_tab_button_;
    ftxui::Component remote_tab_button_;
    ftxui::Component tags_tab_button_;
    ftxui::Component server_tab_button_;
    ftxui::Component compare_tab_button_;
    ftxui::Component tab_buttons_;

    ftxui::Component status_menu_;
    ftxui::Component status_list_component_;
    ftxui::Component refresh_button_;
    ftxui::Component open_button_;
    ftxui::Component stage_all_button_;
    ftxui::Component unstage_all_button_;
    ftxui::Component copy_paths_button_;

    ftxui::Component working_diff_button_;
    ftxui::Component staged_diff_button_;
    ftxui::Component refresh_diff_button_;
    ftxui::Component copy_diff_button_;

    ftxui::Component commit_file_menu_;
    ftxui::Component commit_message_input_;
    ftxui::Component commit_button_;

    ftxui::Component branch_menu_;
    ftxui::Component checkout_button_;
    ftxui::Component merge_button_;
    ftxui::Component rebase_button_;
    ftxui::Component rename_button_;
    ftxui::Component delete_branch_button_;
    ftxui::Component update_button_;
    ftxui::Component rename_branch_input_component_;

    ftxui::Component remote_filter_input_component_;
    ftxui::Component remote_branch_menu_;
    ftxui::Component refresh_remote_button_;
    ftxui::Component checkout_remote_button_;
    ftxui::Component delete_remote_button_;

    ftxui::Component tag_filter_input_component_;
    ftxui::Component tag_menu_;
    ftxui::Component tag_name_input_component_;
    ftxui::Component tag_message_input_component_;
    ftxui::Component create_tag_button_;
    ftxui::Component delete_tag_button_;
    ftxui::Component push_tag_button_;
    ftxui::Component push_all_tags_button_;
    ftxui::Component fetch_tags_button_;

    ftxui::Component confirm_input_component_;
    ftxui::Component confirm_confirm_button_;
    ftxui::Component confirm_cancel_button_;
    ftxui::Component confirm_container_;

    ftxui::Component check_connection_button_;
    ftxui::Component fetch_button_;
    ftxui::Component push_button_;
    ftxui::Component force_push_button_;

    ftxui::Component compare_left_ref_menu_;
    ftxui::Component compare_right_ref_menu_;
    ftxui::Component compare_file_menu_;
    ftxui::Component refresh_compare_refs_button_;
    ftxui::Component refresh_compare_files_button_;
    ftxui::Component open_compare_side_button_;
    ftxui::Component open_compare_diff_button_;
    ftxui::Component copy_compare_diff_button_;

    ftxui::Component status_tab_container_;
    ftxui::Component diff_tab_container_;
    ftxui::Component commit_tab_container_;
    ftxui::Component branches_tab_container_;
    ftxui::Component remote_tab_container_;
    ftxui::Component tags_tab_container_;
    ftxui::Component server_tab_container_;
    ftxui::Component compare_tab_container_;
    ftxui::Component tab_body_container_;
    ftxui::Component container_;
};

class GitModal {
public:
    using OpenFileCallback = GitModalContent::OpenFileCallback;
    using OpenCompareCallback = GitModalContent::OpenCompareCallback;
    using WriteClipboardCallback = GitModalContent::WriteClipboardCallback;

    GitModal(
        const Theme* theme,
        GitManager* git_manager,
        OpenFileCallback on_open_file,
        OpenCompareCallback on_open_compare,
        WriteClipboardCallback write_clipboard,
        GitModalContent::RequestRedrawCallback request_redraw = {});

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<GitModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
