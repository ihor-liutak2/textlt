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

bool ContainsFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) {
        return true;
    }
    return text.find(filter) != std::string::npos;
}

} // namespace

void GitModalContent::RefreshRemoteBranches() {
    remote_branches_.clear();
    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }
    remote_branches_ = git_manager_->GetRemoteBranches();
    RebuildRemoteBranchLabels();
    status_ = "Remote branches refreshed.";
}

void GitModalContent::CheckoutSelectedRemoteBranch() {
    if (!git_manager_) {
        return;
    }
    if (HasUncommittedChanges()) {
        status_ = "Cannot checkout remote branch: repository has uncommitted changes.";
        return;
    }
    if (selected_remote_branch_ < 0 ||
        selected_remote_branch_ >= static_cast<int>(filtered_remote_branches_.size())) {
        status_ = "No remote branch selected.";
        return;
    }
    const std::string branch = filtered_remote_branches_[static_cast<size_t>(selected_remote_branch_)];
    RequestConfirm(
        "Confirm remote checkout",
        "Create a local tracking branch from the selected remote branch.",
        "git checkout --track " + branch,
        [this, branch] {
            RunAndRefresh("Checkout remote", git_manager_->CheckoutRemoteBranch(branch));
        });
}

void GitModalContent::DeleteSelectedRemoteBranch() {
    if (!git_manager_) {
        return;
    }
    if (selected_remote_branch_ < 0 ||
        selected_remote_branch_ >= static_cast<int>(filtered_remote_branches_.size())) {
        status_ = "No remote branch selected.";
        return;
    }
    const std::string branch = filtered_remote_branches_[static_cast<size_t>(selected_remote_branch_)];
    RequestConfirm(
        "Confirm remote branch delete",
        "Delete selected branch from the remote repository.",
        "git push <remote> --delete <branch>",
        [this, branch] {
            RunAndRefresh("Delete remote branch", git_manager_->DeleteRemoteBranch(branch));
            RefreshRemoteBranches();
        },
        "DELETE");
}

void GitModalContent::RefreshTags() {
    tags_.clear();
    if (!git_manager_) {
        status_ = "Git manager is not configured.";
        return;
    }
    tags_ = git_manager_->GetTags();
    RebuildTagLabels();
}

void GitModalContent::CreateTag() {
    if (!git_manager_) {
        return;
    }
    const std::string tag_name = TrimCopy(tag_name_input_);
    const std::string message = TrimCopy(tag_message_input_);
    if (tag_name.empty()) {
        status_ = "Tag name is empty.";
        return;
    }
    RequestConfirm(
        "Confirm tag create",
        message.empty() ? "Create lightweight tag." : "Create annotated tag.",
        message.empty()
            ? "git tag " + tag_name
            : "git tag -a " + tag_name + " -m \"" + TrimForDisplay(message, 70) + "\"",
        [this, tag_name, message] {
            RunAndRefresh("Create tag", git_manager_->CreateTag(tag_name, message));
            RefreshTags();
        });
}

void GitModalContent::DeleteSelectedTag() {
    if (!git_manager_) {
        return;
    }
    if (selected_tag_ < 0 || selected_tag_ >= static_cast<int>(filtered_tags_.size())) {
        status_ = "No tag selected.";
        return;
    }
    const std::string tag = filtered_tags_[static_cast<size_t>(selected_tag_)];
    RequestConfirm(
        "Confirm tag delete",
        "Delete selected local tag.",
        "git tag -d " + tag,
        [this, tag] {
            RunAndRefresh("Delete tag", git_manager_->DeleteTag(tag));
            RefreshTags();
        },
        "DELETE");
}

void GitModalContent::PushSelectedTag() {
    if (!git_manager_) {
        return;
    }
    std::string tag;
    if (selected_tag_ >= 0 && selected_tag_ < static_cast<int>(filtered_tags_.size())) {
        tag = filtered_tags_[static_cast<size_t>(selected_tag_)];
    } else {
        tag = TrimCopy(tag_name_input_);
    }
    if (tag.empty()) {
        status_ = "No tag selected.";
        return;
    }
    RequestConfirm(
        "Confirm tag push",
        "Push selected tag to origin.",
        "git push origin " + tag,
        [this, tag] {
            RunAndRefresh("Push tag", git_manager_->PushTag(tag));
        });
}

void GitModalContent::PushAllTags() {
    if (!git_manager_) {
        return;
    }
    RequestConfirm(
        "Confirm push all tags",
        "Push all local tags to origin.",
        "git push origin --tags",
        [this] {
            RunAndRefresh("Push all tags", git_manager_->PushAllTags());
        });
}

void GitModalContent::FetchTags() {
    if (!git_manager_) {
        return;
    }
    GitManager::CommandResult result = git_manager_->FetchTags();
    RunAndRefresh("Fetch tags", result);
    RefreshTags();
}

void GitModalContent::RebuildRemoteBranchLabels() {
    filtered_remote_branches_.clear();
    remote_branch_labels_.clear();
    for (const std::string& branch : remote_branches_) {
        if (!ContainsFilter(branch, remote_branch_filter_)) {
            continue;
        }
        filtered_remote_branches_.push_back(branch);
        remote_branch_labels_.push_back(branch);
    }
    if (selected_remote_branch_ >= static_cast<int>(remote_branch_labels_.size())) {
        selected_remote_branch_ = static_cast<int>(remote_branch_labels_.size()) - 1;
    }
    if (selected_remote_branch_ < 0) {
        selected_remote_branch_ = 0;
    }
}

void GitModalContent::RebuildTagLabels() {
    filtered_tags_.clear();
    tag_labels_.clear();
    for (const std::string& tag : tags_) {
        if (!ContainsFilter(tag, tag_filter_)) {
            continue;
        }
        filtered_tags_.push_back(tag);
        tag_labels_.push_back(tag);
    }
    if (selected_tag_ >= static_cast<int>(tag_labels_.size())) {
        selected_tag_ = static_cast<int>(tag_labels_.size()) - 1;
    }
    if (selected_tag_ < 0) {
        selected_tag_ = 0;
    }
}

ftxui::Element GitModalContent::RenderRemoteTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::hbox({
            ftxui::text("Remote branches") | ftxui::bold | ftxui::color(theme.modal_accent),
            ftxui::filler(),
            refresh_remote_button_->Render(),
            ftxui::text(" "),
            checkout_remote_button_->Render(),
            ftxui::text(" "),
            delete_remote_button_->Render(),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            ftxui::text("Search: ") | ftxui::color(theme.modal_text_color),
            remote_filter_input_component_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
        }),
        remote_branch_labels_.empty()
            ? (ftxui::text("No remote branches.") | ftxui::color(theme.modal_text_color) | ftxui::frame | ftxui::yflex)
            : (remote_branch_menu_->Render() | ftxui::vscroll_indicator | ftxui::frame | ftxui::yflex),
    });
}

ftxui::Element GitModalContent::RenderTagsTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ftxui::Element tag_list = tag_labels_.empty()
        ? (ftxui::text("No tags.") | ftxui::color(theme.modal_text_color) |
           ftxui::frame | ftxui::yflex)
        : (tag_menu_->Render() | ftxui::vscroll_indicator |
           ftxui::frame | ftxui::yflex);
    ftxui::Element padded_tag_list = ftxui::vbox({
        ftxui::text(""),
        ftxui::hbox({
            ftxui::text(" "),
            tag_list | ftxui::yflex,
            ftxui::text("          "),
        }) | ftxui::yflex,
        ftxui::text(""),
    }) | ftxui::yflex;

    return ftxui::vbox({
        ftxui::hbox({
            ftxui::text("Tags") | ftxui::bold | ftxui::color(theme.modal_accent),
            ftxui::filler(),
            create_tag_button_->Render(),
            ftxui::text(" "),
            delete_tag_button_->Render(),
            ftxui::text(" "),
            push_tag_button_->Render(),
            ftxui::text(" "),
            push_all_tags_button_->Render(),
            ftxui::text(" "),
            fetch_tags_button_->Render(),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            ftxui::text("Search: ") | ftxui::color(theme.modal_text_color),
            tag_filter_input_component_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
        }),
        padded_tag_list,
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            ftxui::text("Tag name: ") | ftxui::color(theme.modal_text_color),
            tag_name_input_component_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
        }),
        ftxui::hbox({
            ftxui::text("Message:  ") | ftxui::color(theme.modal_text_color),
            tag_message_input_component_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
        }),
    });
}

} // namespace textlt
