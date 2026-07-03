#include "modals/modal_git.hpp"

#include <algorithm>
#include <sstream>

#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

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

    GitManager::CommandResult result = git_manager_->DiffFile(path, diff_staged_);

    diff_text_ = result.output;
    diff_scroll_offset_ = 0;
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

    if (selected_commit_file_ >= static_cast<int>(commit_labels_.size())) {
        selected_commit_file_ = static_cast<int>(commit_labels_.size()) - 1;
    }
    if (selected_commit_file_ < 0) {
        selected_commit_file_ = 0;
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

    const std::string message = commit_message_;
    RequestConfirm(
        "Confirm commit",
        "Commit staged files with the current commit message.",
        "git commit -m \"" + TrimForDisplay(message, 80) + "\"",
        [this, message] {
            StartBackgroundOperation(
                BackgroundOperation::Commit,
                "Commit",
                [this, message] { return git_manager_->Commit(message); });
        });
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


void GitModalContent::RebuildCommitLabels() {
    commit_labels_.clear();
    commit_labels_.reserve(commit_files_.size());
    for (const std::string& file : commit_files_) {
        commit_labels_.push_back("[x] " + file);
    }
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


ftxui::Element GitModalContent::RenderStatusTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::text(""),
        ftxui::text("Branch: " + (branch_.empty() ? "-" : branch_)) |
            ftxui::color(theme.modal_text_color),
        ftxui::text("Repo: " + (RepositoryPath().empty() ? "-" : RepositoryPath().generic_string())) |
            ftxui::color(theme.modal_text_color),
        ftxui::text(""),
        ftxui::text("Files:") | ftxui::bold | ftxui::color(theme.modal_accent),
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
            ftxui::text("  Mode: " + std::string(diff_staged_ ? "Staged" : "Working")) |
                ftxui::color(theme.modal_text_color) | ftxui::dim,
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        RenderTextLines(diff_lines_, "No diff loaded.", diff_scroll_offset_) | ftxui::yflex,
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

    ftxui::Element staged_files_view = commit_labels_.empty()
        ? (ftxui::vbox(std::move(file_rows)) | ftxui::frame)
        : (commit_file_menu_->Render() |
           ftxui::vscroll_indicator |
           ftxui::frame);

    return ftxui::vbox({
        ftxui::text("Files to commit:") | ftxui::bold | ftxui::color(theme.modal_accent),
        staged_files_view | ftxui::yflex,
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::text("Commit message:") | ftxui::bold | ftxui::color(theme.modal_accent),
        commit_message_input_->Render() |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg) |
            ftxui::frame |
            ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 5),
    });
}

} // namespace textlt
