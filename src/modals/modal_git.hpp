#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
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
    using WriteClipboardCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void()>;

    GitModalContent(
        const Theme* theme,
        GitManager* git_manager,
        OpenFileCallback on_open_file,
        WriteClipboardCallback write_clipboard,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Git"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {118, 34}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override;

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
        Server = 4,
    };

    ftxui::Component MakeTextButton(std::string label, std::function<void()> on_click);
    ftxui::Component MakeTabButton(std::string label, int tab_index);

    void RefreshStatus();
    void RefreshBranches();
    void RefreshDiff();
    void RefreshCommitFiles();
    void RefreshAll();

    void ToggleSelectedFileStaged();
    void StageAllShownFiles();
    void UnstageAllShownFiles();
    void OpenSelectedFile();
    void CopyStatusPaths();

    void CopyDiff();
    void CommitStagedFiles();

    void CheckoutSelectedBranch();
    void MergeSelectedBranch();
    void RenameSelectedBranch();
    void UpdateSelectedBranch();

    void CheckConnection();
    void FetchServer();
    void PushServer();

    void RunAndRefresh(const std::string& action, const GitManager::CommandResult& result);
    bool HasUncommittedChanges() const;
    bool HasStagedFiles() const;
    std::string SelectedFilePath() const;
    std::string SelectedBranchName() const;
    std::filesystem::path RepositoryPath() const;

    void RebuildStatusLabels();
    void RebuildBranchLabels();
    void RebuildCommitLabels();
    void SplitOutputLines(const std::string& output, std::vector<std::string>* lines) const;
    std::string TrimForDisplay(const std::string& text, size_t max_size) const;
    std::string StatusCodeForEntry(const GitManager::StatusEntry& entry) const;

    ftxui::Element RenderStatusTab();
    ftxui::Element RenderDiffTab();
    ftxui::Element RenderCommitTab();
    ftxui::Element RenderBranchesTab();
    ftxui::Element RenderServerTab();
    ftxui::Element RenderTextLines(const std::vector<std::string>& lines, const std::string& empty_text) const;

    const Theme* theme_ = nullptr;
    GitManager* git_manager_ = nullptr;
    OpenFileCallback on_open_file_;
    WriteClipboardCallback write_clipboard_;
    CloseCallback on_close_;

    int selected_tab_ = 0;
    std::string status_ = "Ready";
    std::string branch_;
    std::filesystem::path repo_root_;

    std::vector<GitManager::StatusEntry> status_entries_;
    std::vector<std::string> status_labels_;
    int selected_status_ = 0;

    std::string diff_text_;
    std::vector<std::string> diff_lines_;

    std::vector<std::string> commit_files_;
    std::vector<std::string> commit_labels_;
    int selected_commit_file_ = 0;
    std::string commit_message_;

    std::vector<std::string> branches_;
    std::vector<std::string> branch_labels_;
    int selected_branch_ = 0;
    std::string rename_branch_input_;

    std::string server_output_;
    std::vector<std::string> server_output_lines_;

    ftxui::Component status_tab_button_;
    ftxui::Component diff_tab_button_;
    ftxui::Component commit_tab_button_;
    ftxui::Component branches_tab_button_;
    ftxui::Component server_tab_button_;
    ftxui::Component tab_buttons_;

    ftxui::Component status_menu_;
    ftxui::Component status_list_component_;
    ftxui::Component refresh_button_;
    ftxui::Component open_button_;
    ftxui::Component stage_all_button_;
    ftxui::Component unstage_all_button_;
    ftxui::Component copy_paths_button_;

    ftxui::Component refresh_diff_button_;
    ftxui::Component copy_diff_button_;

    ftxui::Component commit_file_menu_;
    ftxui::Component commit_message_input_;
    ftxui::Component commit_button_;

    ftxui::Component branch_menu_;
    ftxui::Component checkout_button_;
    ftxui::Component merge_button_;
    ftxui::Component rename_button_;
    ftxui::Component update_button_;
    ftxui::Component rename_branch_input_component_;

    ftxui::Component check_connection_button_;
    ftxui::Component fetch_button_;
    ftxui::Component push_button_;

    ftxui::Component status_tab_container_;
    ftxui::Component diff_tab_container_;
    ftxui::Component commit_tab_container_;
    ftxui::Component branches_tab_container_;
    ftxui::Component server_tab_container_;
    ftxui::Component tab_body_container_;
    ftxui::Component container_;
};

class GitModal {
public:
    using OpenFileCallback = GitModalContent::OpenFileCallback;
    using WriteClipboardCallback = GitModalContent::WriteClipboardCallback;

    GitModal(
        const Theme* theme,
        GitManager* git_manager,
        OpenFileCallback on_open_file,
        WriteClipboardCallback write_clipboard);

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
