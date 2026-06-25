#include "modals/modal_git.hpp"

#include <algorithm>
#include <sstream>

#include "ftxui/dom/elements.hpp"

namespace textlt {

void GitModalContent::RefreshAll() {
    RefreshStatus();
    RefreshBranches();
    if (selected_tab_ == static_cast<int>(Tab::Diff)) {
        RefreshDiff();
    }
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


} // namespace textlt
