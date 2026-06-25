#include "modals/modal_git.hpp"

#include <algorithm>

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

} // namespace

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

} // namespace textlt
