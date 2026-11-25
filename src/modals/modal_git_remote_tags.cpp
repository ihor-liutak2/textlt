#include "modals/modal_git.hpp"

#include <algorithm>
#include <sstream>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"

namespace textlt {
namespace {

class TagColumn final : public ftxui::Node {
public:
    explicit TagColumn(ftxui::Element content)
        : Node(ftxui::unpack(std::move(content))) {}

private:
    void ComputeRequirement() override {
        Node::ComputeRequirement();
        // Give every column the same base width before hbox distributes space.
        requirement_.min_x = 0;
    }

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        children_.front()->SetBox(box);
    }
};

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
    selected_tag_ = 0;
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

void GitModalContent::MoveSelectedTag() {
    if (!git_manager_) {
        return;
    }
    if (selected_tag_ < 0 || selected_tag_ >= static_cast<int>(filtered_tags_.size())) {
        status_ = "No tag selected.";
        return;
    }
    const std::string tag = filtered_tags_[static_cast<size_t>(selected_tag_)];
    RequestConfirm(
        "Confirm tag move",
        "Move tag \"" + TrimForDisplay(tag, 40) +
            "\" to the latest commit (HEAD). Remote tag is not updated.",
        "git tag -f " + tag + " HEAD",
        [this, tag] {
            RunAndRefresh("Move tag", git_manager_->MoveTag(tag));
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
        "Push selected tag to origin. Force push overwrites the remote tag.",
        "git push origin " + tag,
        [this, tag] {
            const bool force = confirm_force_tag_push_;
            StartBackgroundOperation(
                BackgroundOperation::PushTag,
                "Push tag",
                [this, tag, force] { return git_manager_->PushTag(tag, force); });
        },
        "",
        true,
        "git push --force origin " + tag);
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
            StartBackgroundOperation(
                BackgroundOperation::PushAllTags,
                "Push all tags",
                [this] { return git_manager_->PushAllTags(); });
        });
}

void GitModalContent::FetchTags() {
    if (!git_manager_) {
        return;
    }
    StartBackgroundOperation(
        BackgroundOperation::FetchTags,
        "Fetch tags",
        [this] { return git_manager_->FetchTags(); });
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
    const size_t rows = tag_labels_.size() / tag_column_labels_.size();
    const size_t extra_rows = tag_labels_.size() % tag_column_labels_.size();
    size_t offset = 0;
    for (size_t column = 0; column < tag_column_labels_.size(); ++column) {
        const size_t count = rows + (column < extra_rows ? 1 : 0);
        tag_column_offsets_[column] = static_cast<int>(offset);
        tag_column_labels_[column].assign(
            tag_labels_.begin() + offset, tag_labels_.begin() + offset + count);
        offset += count;
    }
    tag_column_selection_.fill(0);
    SelectTag(selected_tag_);
}

void GitModalContent::SelectTag(int index) {
    selected_tag_ = index;
    for (size_t column = 0; column < tag_column_labels_.size(); ++column) {
        const int row = index - tag_column_offsets_[column];
        if (row >= 0 && row < static_cast<int>(tag_column_labels_[column].size())) {
            tag_column_selection_[column] = row;
            tag_column_menus_container_->SetActiveChild(tag_column_menus_[column]);
            break;
        }
    }
}

ftxui::Element GitModalContent::RenderRemoteTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::text("Remote branches") | ftxui::bold | ftxui::color(theme.modal_accent),
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
    ftxui::Elements columns;
    if (!tag_labels_.empty()) {
        for (const auto& menu : tag_column_menus_) {
            auto viewport = menu->Render() | ftxui::vscroll_indicator | ftxui::yframe;
            columns.push_back(std::make_shared<TagColumn>(std::move(viewport)) | ftxui::xflex);
        }
    }
    ftxui::Element tag_list = tag_labels_.empty()
        ? (ftxui::text("No tags.") | ftxui::color(theme.modal_text_color) |
           ftxui::frame | ftxui::yflex)
        : (ftxui::hbox(std::move(columns)) | ftxui::flex);
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
        ftxui::text("Tags") | ftxui::bold | ftxui::color(theme.modal_accent),
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
