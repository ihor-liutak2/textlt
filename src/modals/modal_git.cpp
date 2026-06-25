#include "modals/modal_git.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

std::string BracketLabel(const std::string& label) {
    return !label.empty() && label.front() == '[' ? label : "[" + label + "]";
}


std::string TrimCopy(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() &&
           (value[start] == '\n' || value[start] == '\r' || value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::string PathListToText(const std::vector<std::string>& paths) {
    std::ostringstream stream;
    for (size_t index = 0; index < paths.size(); ++index) {
        if (index > 0) {
            stream << '\n';
        }
        stream << paths[index];
    }
    return stream.str();
}

} // namespace

GitModalContent::GitModalContent(
    const Theme* theme,
    GitManager* git_manager,
    OpenFileCallback on_open_file,
    WriteClipboardCallback write_clipboard,
    CloseCallback on_close)
    : theme_(theme),
      git_manager_(git_manager),
      on_open_file_(std::move(on_open_file)),
      write_clipboard_(std::move(write_clipboard)),
      on_close_(std::move(on_close)) {
    status_tab_button_ = MakeTabButton("Status", static_cast<int>(Tab::Status));
    diff_tab_button_ = MakeTabButton("Diff", static_cast<int>(Tab::Diff));
    commit_tab_button_ = MakeTabButton("Commit", static_cast<int>(Tab::Commit));
    branches_tab_button_ = MakeTabButton("Branches", static_cast<int>(Tab::Branches));
    server_tab_button_ = MakeTabButton("Server", static_cast<int>(Tab::Server));

    tab_buttons_ = ftxui::Container::Horizontal({
        status_tab_button_,
        diff_tab_button_,
        commit_tab_button_,
        branches_tab_button_,
        server_tab_button_,
    });

    ftxui::MenuOption status_menu_option = ftxui::MenuOption::Vertical();
    status_menu_option.on_enter = [this] { ToggleSelectedFileStaged(); };
    status_menu_option.on_change = [this] {
        if (selected_tab_ == static_cast<int>(Tab::Diff)) {
            RefreshDiff();
        }
    };
    status_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    status_menu_ = ftxui::Menu(&status_labels_, &selected_status_, status_menu_option);
    status_list_component_ = ftxui::CatchEvent(status_menu_, [this](ftxui::Event event) {
        if (event == ftxui::Event::Character(" ")) {
            ToggleSelectedFileStaged();
            return true;
        }
        return false;
    });

    refresh_button_ = MakeTextButton("Refresh", [this] { RefreshAll(); });
    open_button_ = MakeTextButton("Open", [this] { OpenSelectedFile(); });
    stage_all_button_ = MakeTextButton("Stage all", [this] { StageAllShownFiles(); });
    unstage_all_button_ = MakeTextButton("Unstage all", [this] { UnstageAllShownFiles(); });
    copy_paths_button_ = MakeTextButton("Copy paths", [this] { CopyStatusPaths(); });

    refresh_diff_button_ = MakeTextButton("Refresh diff", [this] { RefreshDiff(); });
    copy_diff_button_ = MakeTextButton("Copy diff", [this] { CopyDiff(); });

    ftxui::InputOption commit_message_option;
    commit_message_option.multiline = true;
    commit_message_input_ = ftxui::Input(&commit_message_, "commit message", commit_message_option);
    commit_button_ = MakeTextButton("Commit", [this] { CommitStagedFiles(); });

    ftxui::MenuOption branch_menu_option = ftxui::MenuOption::Vertical();
    branch_menu_option.on_change = [this] {
        rename_branch_input_ = SelectedBranchName();
    };
    branch_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    branch_menu_ = ftxui::Menu(&branch_labels_, &selected_branch_, branch_menu_option);
    checkout_button_ = MakeTextButton("Checkout", [this] { CheckoutSelectedBranch(); });
    merge_button_ = MakeTextButton("Merge", [this] { MergeSelectedBranch(); });
    rename_button_ = MakeTextButton("Rename", [this] { RenameSelectedBranch(); });
    update_button_ = MakeTextButton("Update", [this] { UpdateSelectedBranch(); });

    ftxui::InputOption rename_branch_option;
    rename_branch_option.multiline = false;
    rename_branch_input_component_ =
        ftxui::Input(&rename_branch_input_, "new branch name", rename_branch_option);

    check_connection_button_ = MakeTextButton("Check connection", [this] { CheckConnection(); });
    fetch_button_ = MakeTextButton("Fetch", [this] { FetchServer(); });
    push_button_ = MakeTextButton("Push", [this] { PushServer(); });

    status_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            refresh_button_,
            open_button_,
            stage_all_button_,
            unstage_all_button_,
            copy_paths_button_,
        }),
        status_list_component_,
    });

    diff_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            refresh_diff_button_,
            copy_diff_button_,
        }),
    });

    commit_tab_container_ = ftxui::Container::Vertical({
        commit_message_input_,
        commit_button_,
    });

    branches_tab_container_ = ftxui::Container::Vertical({
        branch_menu_,
        rename_branch_input_component_,
        ftxui::Container::Horizontal({
            checkout_button_,
            merge_button_,
            rename_button_,
            update_button_,
        }),
    });

    server_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            check_connection_button_,
            fetch_button_,
            push_button_,
        }),
    });

    tab_body_container_ = ftxui::Container::Tab({
        status_tab_container_,
        diff_tab_container_,
        commit_tab_container_,
        branches_tab_container_,
        server_tab_container_,
    }, &selected_tab_);

    container_ = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
}

ftxui::Component GitModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text(BracketLabel(state.label));
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };
    return ftxui::Button(option);
}

ftxui::Component GitModalContent::MakeTabButton(std::string label, int tab_index) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = [this, tab_index] {
        selected_tab_ = tab_index;
        if (selected_tab_ == static_cast<int>(Tab::Status) && status_list_component_) {
            status_list_component_->TakeFocus();
        } else if (selected_tab_ == static_cast<int>(Tab::Branches) && branch_menu_) {
            branch_menu_->TakeFocus();
        } else if (selected_tab_ == static_cast<int>(Tab::Commit) && commit_message_input_) {
            commit_message_input_->TakeFocus();
        }
    };
    option.transform = [this, tab_index](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element tab = ftxui::text(BracketLabel(state.label));
        if (selected_tab_ == tab_index || state.focused || state.active) {
            return tab |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return tab | ftxui::color(theme.modal_text_color) | ftxui::dim;
    };
    return ftxui::Button(option);
}

void GitModalContent::Open() {
    selected_tab_ = static_cast<int>(Tab::Status);
    status_ = "Ready";
    server_output_.clear();
    server_output_lines_.clear();
    RefreshAll();
    if (status_list_component_) {
        status_list_component_->TakeFocus();
    }
}

void GitModalContent::Close() {
}

ftxui::Element GitModalContent::RenderTitle() {
    return ftxui::hbox({
        status_tab_button_->Render(),
        ftxui::text(" "),
        diff_tab_button_->Render(),
        ftxui::text(" "),
        commit_tab_button_->Render(),
        ftxui::text(" "),
        branches_tab_button_->Render(),
        ftxui::text(" "),
        server_tab_button_->Render(),
    });
}

ftxui::Element GitModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Element body;
    if (selected_tab_ == static_cast<int>(Tab::Status)) {
        body = RenderStatusTab();
    } else if (selected_tab_ == static_cast<int>(Tab::Diff)) {
        body = RenderDiffTab();
    } else if (selected_tab_ == static_cast<int>(Tab::Commit)) {
        body = RenderCommitTab();
    } else if (selected_tab_ == static_cast<int>(Tab::Branches)) {
        body = RenderBranchesTab();
    } else {
        body = RenderServerTab();
    }

    return body |
        ftxui::bgcolor(theme.modal_background) |
        ftxui::color(theme.modal_foreground);
}

std::string GitModalContent::GetFooterText() const {
    return status_.empty() ? "Git" : status_;
}

bool GitModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        if (on_close_) {
            on_close_();
        }
        return true;
    }

    if (event == ftxui::Event::Character(" ") &&
        selected_tab_ == static_cast<int>(Tab::Status) &&
        status_list_component_ &&
        status_list_component_->Focused()) {
        ToggleSelectedFileStaged();
        return true;
    }

    return false;
}

void GitModalContent::RefreshStatus() {
    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }

    git_manager_->RefreshNow();
    branch_ = git_manager_->GetCurrentBranch();
    repo_root_ = git_manager_->RepositoryRoot();
    status_entries_ = git_manager_->GetStatusEntries();
    RebuildStatusLabels();
    RefreshCommitFiles();

    if (repo_root_.empty()) {
        status_ = "No Git repository found.";
        return;
    }

    status_ = status_entries_.empty()
        ? "Working tree clean."
        : std::to_string(status_entries_.size()) + " changed file(s).";
}

void GitModalContent::RefreshBranches() {
    branches_.clear();
    if (!git_manager_) {
        return;
    }

    std::string current;
    branches_ = git_manager_->GetLocalBranches(&current);
    if (!current.empty()) {
        branch_ = current;
    }
    RebuildBranchLabels();
    rename_branch_input_ = SelectedBranchName();
}

void GitModalContent::RefreshDiff() {
    diff_text_.clear();
    diff_lines_.clear();

    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }

    const std::string path = SelectedFilePath();
    if (path.empty()) {
        status_ = "No file selected for diff.";
        return;
    }

    bool staged = false;
    if (selected_status_ >= 0 && selected_status_ < static_cast<int>(status_entries_.size())) {
        staged = status_entries_[static_cast<size_t>(selected_status_)].IsStaged();
    }

    GitManager::CommandResult result = git_manager_->DiffFile(path, staged);
    if (staged && result.output.empty()) {
        result = git_manager_->DiffFile(path, false);
    }

    diff_text_ = result.output;
    SplitOutputLines(diff_text_, &diff_lines_);
    status_ = result.success()
        ? "Diff refreshed."
        : "Diff failed: " + TrimForDisplay(result.output, 120);
}

void GitModalContent::RefreshCommitFiles() {
    commit_files_.clear();
    for (const GitManager::StatusEntry& entry : status_entries_) {
        if (entry.IsStaged()) {
            commit_files_.push_back(entry.path);
        }
    }
    RebuildCommitLabels();
}

void GitModalContent::RefreshAll() {
    RefreshStatus();
    RefreshBranches();
    if (selected_tab_ == static_cast<int>(Tab::Diff)) {
        RefreshDiff();
    }
}

void GitModalContent::ToggleSelectedFileStaged() {
    if (!git_manager_ || status_entries_.empty()) {
        status_ = "No file selected.";
        return;
    }

    selected_status_ = std::max(0, std::min(
        selected_status_,
        static_cast<int>(status_entries_.size()) - 1));
    const GitManager::StatusEntry entry = status_entries_[static_cast<size_t>(selected_status_)];
    GitManager::CommandResult result = entry.IsStaged()
        ? git_manager_->UnstageFile(entry.path)
        : git_manager_->StageFile(entry.path);
    RunAndRefresh(entry.IsStaged() ? "Unstage" : "Stage", result);
}

void GitModalContent::StageAllShownFiles() {
    if (!git_manager_ || status_entries_.empty()) {
        status_ = "No files to stage.";
        return;
    }

    GitManager::CommandResult last_result;
    size_t count = 0;
    for (const GitManager::StatusEntry& entry : status_entries_) {
        last_result = git_manager_->StageFile(entry.path);
        if (!last_result.success()) {
            RunAndRefresh("Stage all", last_result);
            return;
        }
        ++count;
    }

    RefreshAll();
    status_ = "Staged " + std::to_string(count) + " file(s).";
}

void GitModalContent::UnstageAllShownFiles() {
    if (!git_manager_ || status_entries_.empty()) {
        status_ = "No files to unstage.";
        return;
    }

    GitManager::CommandResult last_result;
    size_t count = 0;
    for (const GitManager::StatusEntry& entry : status_entries_) {
        if (!entry.IsStaged()) {
            continue;
        }
        last_result = git_manager_->UnstageFile(entry.path);
        if (!last_result.success()) {
            RunAndRefresh("Unstage all", last_result);
            return;
        }
        ++count;
    }

    RefreshAll();
    status_ = "Unstaged " + std::to_string(count) + " file(s).";
}

void GitModalContent::OpenSelectedFile() {
    if (!on_open_file_) {
        status_ = "Open callback is not configured.";
        return;
    }

    const std::string path = SelectedFilePath();
    if (path.empty()) {
        status_ = "No file selected.";
        return;
    }

    const std::filesystem::path full_path = RepositoryPath() / path;
    std::string error;
    if (!on_open_file_(full_path, error)) {
        status_ = error.empty() ? "Unable to open selected file." : error;
        return;
    }

    if (on_close_) {
        on_close_();
    }
}

void GitModalContent::CopyStatusPaths() {
    if (!write_clipboard_) {
        status_ = "Clipboard write is not configured.";
        return;
    }

    std::vector<std::string> paths;
    for (const GitManager::StatusEntry& entry : status_entries_) {
        paths.push_back((RepositoryPath() / entry.path).lexically_normal().generic_string());
    }

    if (paths.empty()) {
        status_ = "No paths to copy.";
        return;
    }

    write_clipboard_(PathListToText(paths));
    status_ = "Copied " + std::to_string(paths.size()) + " path(s).";
}

void GitModalContent::CopyDiff() {
    if (!write_clipboard_) {
        status_ = "Clipboard write is not configured.";
        return;
    }
    if (diff_text_.empty()) {
        status_ = "No diff to copy.";
        return;
    }
    write_clipboard_(diff_text_);
    status_ = "Diff copied.";
}

void GitModalContent::CommitStagedFiles() {
    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }
    RefreshStatus();
    if (!HasStagedFiles()) {
        status_ = "Nothing staged for commit.";
        return;
    }
    if (TrimCopy(commit_message_).empty()) {
        status_ = "Commit message is empty.";
        return;
    }

    GitManager::CommandResult result = git_manager_->Commit(commit_message_);
    RunAndRefresh("Commit", result);
    if (result.success()) {
        commit_message_.clear();
    }
}

void GitModalContent::CheckoutSelectedBranch() {
    if (!git_manager_) {
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot checkout: repository has uncommitted changes.";
        return;
    }
    const std::string branch = SelectedBranchName();
    if (branch.empty()) {
        status_ = "No branch selected.";
        return;
    }
    RunAndRefresh("Checkout", git_manager_->CheckoutBranch(branch));
}

void GitModalContent::MergeSelectedBranch() {
    if (!git_manager_) {
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot merge: repository has uncommitted changes.";
        return;
    }
    const std::string branch = SelectedBranchName();
    if (branch.empty() || branch == branch_) {
        status_ = "Select another branch to merge.";
        return;
    }
    GitManager::CommandResult result = git_manager_->MergeBranch(branch);
    RunAndRefresh("Merge", result);
    if (!result.success() && result.output.find("CONFLICT") != std::string::npos) {
        status_ = "Merge has conflicts. Fix conflicted files manually.";
    }
}

void GitModalContent::RenameSelectedBranch() {
    if (!git_manager_) {
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot rename: repository has uncommitted changes.";
        return;
    }
    const std::string old_name = SelectedBranchName();
    const std::string new_name = TrimCopy(rename_branch_input_);
    if (old_name.empty() || new_name.empty()) {
        status_ = "Branch name is empty.";
        return;
    }
    if (old_name == new_name) {
        status_ = "Branch name did not change.";
        return;
    }
    RunAndRefresh("Rename", git_manager_->RenameBranch(old_name, new_name));
}

void GitModalContent::UpdateSelectedBranch() {
    if (!git_manager_) {
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot update: repository has uncommitted changes.";
        return;
    }
    const std::string branch = SelectedBranchName();
    if (branch.empty()) {
        status_ = "No branch selected.";
        return;
    }
    if (branch != branch_) {
        status_ = "Update only works for the active branch.";
        return;
    }
    RunAndRefresh("Update", git_manager_->PullFastForward());
}

void GitModalContent::CheckConnection() {
    if (!git_manager_) {
        return;
    }
    GitManager::CommandResult result = git_manager_->CheckOriginConnection();
    server_output_ = result.output;
    SplitOutputLines(server_output_, &server_output_lines_);
    status_ = result.success() ? "Connection OK." : "Connection check failed.";
}

void GitModalContent::FetchServer() {
    if (!git_manager_) {
        return;
    }
    GitManager::CommandResult result = git_manager_->FetchAllPrune();
    server_output_ = result.output;
    SplitOutputLines(server_output_, &server_output_lines_);
    RunAndRefresh("Fetch", result);
}

void GitModalContent::PushServer() {
    if (!git_manager_) {
        return;
    }
    GitManager::CommandResult result = git_manager_->Push();
    server_output_ = result.output;
    SplitOutputLines(server_output_, &server_output_lines_);
    RunAndRefresh("Push", result);
}

void GitModalContent::RunAndRefresh(
    const std::string& action,
    const GitManager::CommandResult& result) {
    RefreshAll();
    if (result.success()) {
        status_ = action + " completed.";
        return;
    }

    const std::string output = TrimForDisplay(result.output, 140);
    status_ = action + " failed" + (output.empty() ? "." : ": " + output);
}

bool GitModalContent::HasUncommittedChanges() const {
    return !status_entries_.empty();
}

bool GitModalContent::HasStagedFiles() const {
    for (const GitManager::StatusEntry& entry : status_entries_) {
        if (entry.IsStaged()) {
            return true;
        }
    }
    return false;
}

std::string GitModalContent::SelectedFilePath() const {
    if (selected_status_ < 0 || selected_status_ >= static_cast<int>(status_entries_.size())) {
        return "";
    }
    return status_entries_[static_cast<size_t>(selected_status_)].path;
}

std::string GitModalContent::SelectedBranchName() const {
    if (selected_branch_ < 0 || selected_branch_ >= static_cast<int>(branches_.size())) {
        return "";
    }
    return branches_[static_cast<size_t>(selected_branch_)];
}

std::filesystem::path GitModalContent::RepositoryPath() const {
    if (!repo_root_.empty()) {
        return repo_root_;
    }
    return git_manager_ ? git_manager_->RepositoryRoot() : std::filesystem::current_path();
}

void GitModalContent::RebuildStatusLabels() {
    status_labels_.clear();
    status_labels_.reserve(status_entries_.size());
    for (const GitManager::StatusEntry& entry : status_entries_) {
        status_labels_.push_back(
            std::string(entry.IsStaged() ? "[x] " : "[ ] ") +
            StatusCodeForEntry(entry) +
            "  " +
            entry.path);
    }
    if (selected_status_ >= static_cast<int>(status_labels_.size())) {
        selected_status_ = static_cast<int>(status_labels_.size()) - 1;
    }
    if (selected_status_ < 0) {
        selected_status_ = 0;
    }
}

void GitModalContent::RebuildBranchLabels() {
    branch_labels_.clear();
    branch_labels_.reserve(branches_.size());
    for (const std::string& branch : branches_) {
        branch_labels_.push_back((branch == branch_ ? "* " : "  ") + branch);
    }
    if (selected_branch_ >= static_cast<int>(branch_labels_.size())) {
        selected_branch_ = static_cast<int>(branch_labels_.size()) - 1;
    }
    if (selected_branch_ < 0) {
        selected_branch_ = 0;
    }
}

void GitModalContent::RebuildCommitLabels() {
    commit_labels_.clear();
    commit_labels_.reserve(commit_files_.size());
    for (const std::string& file : commit_files_) {
        commit_labels_.push_back("[x] " + file);
    }
}

void GitModalContent::SplitOutputLines(
    const std::string& output,
    std::vector<std::string>* lines) const {
    if (!lines) {
        return;
    }
    lines->clear();
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines->push_back(line);
    }
}

std::string GitModalContent::TrimForDisplay(const std::string& text, size_t max_size) const {
    std::string value = text;
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    std::replace(value.begin(), value.end(), '\n', ' ');
    if (value.size() <= max_size) {
        return value;
    }
    if (max_size <= 3) {
        return value.substr(0, max_size);
    }
    return value.substr(0, max_size - 3) + "...";
}

std::string GitModalContent::StatusCodeForEntry(const GitManager::StatusEntry& entry) const {
    if (entry.index_status == '?' && entry.worktree_status == '?') {
        return "??";
    }
    if (entry.index_status != ' ') {
        return std::string(1, entry.index_status);
    }
    if (entry.worktree_status != ' ') {
        return std::string(1, entry.worktree_status);
    }
    return " ";
}

ftxui::Element GitModalContent::RenderTextLines(
    const std::vector<std::string>& lines,
    const std::string& empty_text) const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (lines.empty()) {
        return ftxui::text(empty_text) | ftxui::color(theme.modal_text_color) | ftxui::frame;
    }

    ftxui::Elements rows;
    for (const std::string& line : lines) {
        rows.push_back(ftxui::text(line) | ftxui::color(theme.modal_text_color));
    }
    return ftxui::vbox(std::move(rows)) | ftxui::vscroll_indicator | ftxui::frame;
}

ftxui::Element GitModalContent::RenderStatusTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::text(""),
        ftxui::text("Branch: " + (branch_.empty() ? "-" : branch_)) |
            ftxui::color(theme.modal_text_color),
        ftxui::text("Repo: " + (RepositoryPath().empty() ? "-" : RepositoryPath().generic_string())) |
            ftxui::color(theme.modal_text_color),
        ftxui::text(""),
        ftxui::hbox({
            ftxui::text("Files:") | ftxui::bold | ftxui::color(theme.modal_accent),
            ftxui::filler(),
            refresh_button_->Render(),
            ftxui::text(" "),
            open_button_->Render(),
            ftxui::text(" "),
            stage_all_button_->Render(),
            ftxui::text(" "),
            unstage_all_button_->Render(),
            ftxui::text(" "),
            copy_paths_button_->Render(),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        status_labels_.empty()
            ? (ftxui::text("Working tree clean.") | ftxui::color(theme.modal_text_color) | ftxui::frame | ftxui::yflex)
            : (status_list_component_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::yflex),
    });
}

ftxui::Element GitModalContent::RenderDiffTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string file = SelectedFilePath();
    return ftxui::vbox({
        ftxui::hbox({
            ftxui::text("File: " + (file.empty() ? "-" : file)) |
                ftxui::color(theme.modal_text_color),
            ftxui::filler(),
            refresh_diff_button_->Render(),
            ftxui::text(" "),
            copy_diff_button_->Render(),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        RenderTextLines(diff_lines_, "No diff loaded.") | ftxui::yflex,
    });
}

ftxui::Element GitModalContent::RenderCommitTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Elements file_rows;
    if (commit_labels_.empty()) {
        file_rows.push_back(ftxui::text("No staged files.") | ftxui::color(theme.modal_text_color));
    } else {
        for (const std::string& label : commit_labels_) {
            file_rows.push_back(ftxui::text(label) | ftxui::color(theme.modal_text_color));
        }
    }

    return ftxui::vbox({
        ftxui::text("Files to commit:") | ftxui::bold | ftxui::color(theme.modal_accent),
        ftxui::vbox(std::move(file_rows)) |
            ftxui::vscroll_indicator |
            ftxui::frame |
            ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 8),
        ftxui::text(""),
        ftxui::text("Commit message:") | ftxui::bold | ftxui::color(theme.modal_accent),
        commit_message_input_->Render() |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg) |
            ftxui::frame |
            ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 5),
        ftxui::hbox({
            ftxui::filler(),
            commit_button_->Render(),
        }),
    });
}

ftxui::Element GitModalContent::RenderBranchesTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::hbox({
            ftxui::text("Local branches") | ftxui::bold | ftxui::color(theme.modal_accent),
            ftxui::filler(),
            checkout_button_->Render(),
            ftxui::text(" "),
            merge_button_->Render(),
            ftxui::text(" "),
            rename_button_->Render(),
            ftxui::text(" "),
            update_button_->Render(),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        branch_labels_.empty()
            ? (ftxui::text("No local branches.") | ftxui::color(theme.modal_text_color) | ftxui::frame | ftxui::yflex)
            : (branch_menu_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::yflex),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            ftxui::text("Rename to: ") | ftxui::color(theme.modal_text_color),
            rename_branch_input_component_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
        }),
    });
}

ftxui::Element GitModalContent::RenderServerTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::hbox({
            check_connection_button_->Render(),
            ftxui::text(" "),
            fetch_button_->Render(),
            ftxui::text(" "),
            push_button_->Render(),
            ftxui::filler(),
            ftxui::text("origin") | ftxui::dim | ftxui::color(theme.modal_text_color),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        RenderTextLines(server_output_lines_, "No server command output yet.") | ftxui::yflex,
    });
}

GitModal::GitModal(
    const Theme* theme,
    GitManager* git_manager,
    OpenFileCallback on_open_file,
    WriteClipboardCallback write_clipboard)
    : theme_(theme) {
    content_ = std::make_shared<GitModalContent>(
        theme_,
        git_manager,
        std::move(on_open_file),
        std::move(write_clipboard),
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
}

ftxui::Component GitModal::View() const {
    return modal_;
}

void GitModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
    }
}

void GitModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool GitModal::IsOpen() const {
    return open_;
}

bool GitModal::OnEvent(ftxui::Event event) {
    if (!open_) {
        return false;
    }
    if (content_ && content_->HandleEvent(event)) {
        return true;
    }
    return modal_ ? modal_->OnEvent(event) : false;
}

} // namespace textlt
