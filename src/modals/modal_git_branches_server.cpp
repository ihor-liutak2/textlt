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

void GitModalContent::RequestBranchInput(
    const std::string& title,
    const std::string& message,
    const std::string& label,
    const std::string& initial_value,
    const std::string& required_text,
    std::function<void()> on_confirm) {
    branch_input_active_ = true;
    confirm_layer_index_ = 2;
    branch_input_title_ = title;
    branch_input_message_ = message;
    branch_input_label_ = label;
    branch_input_value_ = initial_value;
    branch_input_required_text_ = required_text;
    branch_input_callback_ = std::move(on_confirm);

    if (branch_input_component_) {
        branch_input_component_->TakeFocus();
    }
}

void GitModalContent::ConfirmBranchInput() {
    if (!branch_input_active_) {
        return;
    }
    if (!branch_input_required_text_.empty() &&
        branch_input_value_ != branch_input_required_text_) {
        status_ = "Confirmation text does not match.";
        return;
    }
    if (TrimCopy(branch_input_value_).empty()) {
        status_ = "Branch name is required.";
        return;
    }

    auto callback = std::move(branch_input_callback_);
    branch_input_active_ = false;
    confirm_layer_index_ = 0;
    if (callback) {
        callback();
    }
    branch_input_title_.clear();
    branch_input_message_.clear();
    branch_input_label_.clear();
    branch_input_value_.clear();
    branch_input_required_text_.clear();
}

void GitModalContent::CancelBranchInput() {
    branch_input_active_ = false;
    confirm_layer_index_ = 0;
    branch_input_callback_ = nullptr;
    branch_input_title_.clear();
    branch_input_message_.clear();
    branch_input_label_.clear();
    branch_input_value_.clear();
    branch_input_required_text_.clear();
    status_ = "Branch action cancelled.";
}

bool GitModalContent::HandleBranchInputEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        CancelBranchInput();
        return true;
    }
    if (event == ftxui::Event::Return) {
        ConfirmBranchInput();
        return true;
    }
    return branch_input_container_ ? branch_input_container_->OnEvent(event) : true;
}

ftxui::Element GitModalContent::RenderBranchInputOverlay() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::vbox({
        ftxui::text(branch_input_title_) |
            ftxui::bold | ftxui::color(theme.modal_accent),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::text(branch_input_message_) | ftxui::color(theme.modal_text_color),
        ftxui::text(""),
        ftxui::hbox({
            ftxui::text(branch_input_label_ + " ") |
                ftxui::bold | ftxui::color(theme.modal_text_color),
            branch_input_component_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
        }),
        ftxui::text(""),
        ftxui::hbox({
            ftxui::filler(),
            branch_input_confirm_button_->Render(),
            ftxui::text(" "),
            branch_input_cancel_button_->Render(),
        }),
    }) |
        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 96) |
        ftxui::border |
        ftxui::bgcolor(theme.modal_background) |
        ftxui::color(theme.modal_foreground);
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
    RequestConfirm(
        "Confirm checkout",
        "Checkout selected local branch.",
        "git checkout " + branch,
        [this, branch] {
            RunAndRefresh("Checkout", git_manager_->CheckoutBranch(branch));
        });
}

void GitModalContent::CreateNewBranch() {
    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot create branch: repository has uncommitted changes.";
        return;
    }
    RequestBranchInput(
        "Create Branch",
        "Create and check out a new local branch.",
        "Branch name:",
        "",
        "",
        [this] {
            std::string name = TrimCopy(branch_input_value_);
            if (name.empty()) {
                status_ = "Branch name is empty.";
                return;
            }
            for (const auto& existing : branches_) {
                if (existing == name) {
                    status_ = "Branch already exists: " + name;
                    return;
                }
            }
            RunAndRefresh("Create branch", git_manager_->CreateBranch(name));
        });
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
    RequestConfirm(
        "Confirm merge",
        "Merge selected branch into the active branch.",
        "git merge " + branch,
        [this, branch] {
            GitManager::CommandResult result = git_manager_->MergeBranch(branch);
            RunAndRefresh("Merge", result);
            if (!result.success() && result.output.find("CONFLICT") != std::string::npos) {
                status_ = "Merge has conflicts. Fix conflicted files manually.";
            }
        });
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
    if (old_name.empty()) {
        status_ = "No branch selected.";
        return;
    }
    RequestBranchInput(
        "Rename Branch",
        "Rename local branch \"" + old_name + "\".",
        "New name:",
        old_name,
        "",
        [this, old_name] {
            std::string new_name = TrimCopy(branch_input_value_);
            if (new_name.empty()) {
                status_ = "Branch name is empty.";
                return;
            }
            if (new_name == old_name) {
                status_ = "Branch name did not change.";
                return;
            }
            RunAndRefresh("Rename", git_manager_->RenameBranch(old_name, new_name));
        });
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
    RequestConfirm(
        "Confirm update",
        "Update active branch with fast-forward only pull.",
        "git pull --ff-only",
        [this] {
            RunAndRefresh("Update", git_manager_->PullFastForward());
        });
}

void GitModalContent::RebaseSelectedBranch() {
    if (!git_manager_) {
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot rebase: repository has uncommitted changes.";
        return;
    }
    const std::string branch = SelectedBranchName();
    if (branch.empty() || branch == branch_) {
        status_ = "Select another branch to rebase onto.";
        return;
    }
    RequestConfirm(
        "Confirm rebase",
        "Rebase active branch onto the selected branch.",
        "git rebase " + branch,
        [this, branch] {
            RunAndRefresh("Rebase", git_manager_->RebaseBranch(branch));
        });
}

void GitModalContent::DeleteSelectedBranch() {
    if (!git_manager_) {
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot delete branch: repository has uncommitted changes.";
        return;
    }
    const std::string branch = SelectedBranchName();
    if (branch.empty()) {
        status_ = "No branch selected.";
        return;
    }
    if (branch == branch_) {
        status_ = "Cannot delete the active branch.";
        return;
    }
    RequestBranchInput(
        "Delete Branch",
        "Delete local branch \"" + branch + "\". This cannot be undone.",
        "Type DELETE to confirm:",
        "",
        "DELETE",
        [this, branch] {
            RunAndRefresh("Delete branch", git_manager_->DeleteLocalBranch(branch));
        });
}

void GitModalContent::CheckConnection() {
    if (!git_manager_) {
        return;
    }
    StartBackgroundOperation(
        BackgroundOperation::CheckConnection,
        "Check connection",
        [this] { return git_manager_->CheckOriginConnection(); });
}

void GitModalContent::FetchServer() {
    if (!git_manager_) {
        return;
    }
    StartBackgroundOperation(
        BackgroundOperation::Fetch,
        "Fetch",
        [this] { return git_manager_->FetchAllPrune(); });
}

void GitModalContent::PushServer() {
    if (!git_manager_) {
        return;
    }
    RequestConfirm(
        "Confirm push",
        "Push current branch to the configured upstream.",
        "git push",
        [this] {
            StartBackgroundOperation(
                BackgroundOperation::Push,
                "Push",
                [this] { return git_manager_->Push(); });
        });
}

void GitModalContent::ForcePushWithLease() {
    if (!git_manager_) {
        return;
    }
    RequestConfirm(
        "Confirm force push",
        "Force push with lease. This can rewrite remote history.",
        "git push --force-with-lease",
        [this] {
            StartBackgroundOperation(
                BackgroundOperation::ForcePush,
                "Force push with lease",
                [this] { return git_manager_->ForcePushWithLease(); });
        },
        "PUSH");
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
        ftxui::text("Local branches") | ftxui::bold | ftxui::color(theme.modal_accent),
        ftxui::separator() | ftxui::color(theme.modal_border),
        branch_labels_.empty()
            ? (ftxui::text("No local branches.") | ftxui::color(theme.modal_text_color) | ftxui::frame | ftxui::yflex)
            : (branch_menu_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::yflex),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            checkout_button_->Render(),
            ftxui::text(" "),
            merge_button_->Render(),
            ftxui::text(" "),
            rebase_button_->Render(),
            ftxui::text(" "),
            rename_button_->Render(),
            ftxui::text(" "),
            delete_branch_button_->Render(),
            ftxui::text(" "),
            update_button_->Render(),
            ftxui::text(" "),
            new_branch_button_->Render(),
        }),
    });
}

ftxui::Element GitModalContent::RenderServerTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::text("Remote: origin") | ftxui::dim | ftxui::color(theme.modal_text_color),
        ftxui::separator() | ftxui::color(theme.modal_border),
        RenderTextLines(server_output_lines_, "No server command output yet.", server_output_scroll_offset_) | ftxui::yflex,
    });
}

} // namespace textlt
