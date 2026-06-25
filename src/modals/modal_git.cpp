#include "modals/modal_git.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

std::string BracketLabel(const std::string& label) {
    return !label.empty() && label.front() == '[' ? label : "[" + label + "]";
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
    remote_tab_button_ = MakeTabButton("Remote", static_cast<int>(Tab::Remote));
    tags_tab_button_ = MakeTabButton("Tags", static_cast<int>(Tab::Tags));
    server_tab_button_ = MakeTabButton("Server", static_cast<int>(Tab::Server));

    tab_buttons_ = ftxui::Container::Horizontal({
        status_tab_button_,
        diff_tab_button_,
        commit_tab_button_,
        branches_tab_button_,
        remote_tab_button_,
        tags_tab_button_,
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

    working_diff_button_ = MakeTextButton("Working", [this] {
        diff_staged_ = false;
        diff_scroll_offset_ = 0;
        RefreshDiff();
    });
    staged_diff_button_ = MakeTextButton("Staged", [this] {
        diff_staged_ = true;
        diff_scroll_offset_ = 0;
        RefreshDiff();
    });
    refresh_diff_button_ = MakeTextButton("Refresh diff", [this] { RefreshDiff(); });
    copy_diff_button_ = MakeTextButton("Copy diff", [this] { CopyDiff(); });

    ftxui::MenuOption commit_file_menu_option = ftxui::MenuOption::Vertical();
    commit_file_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
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
    commit_file_menu_ = ftxui::Menu(&commit_labels_, &selected_commit_file_, commit_file_menu_option);

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
    rebase_button_ = MakeTextButton("Rebase", [this] { RebaseSelectedBranch(); });
    rename_button_ = MakeTextButton("Rename", [this] { RenameSelectedBranch(); });
    delete_branch_button_ = MakeTextButton("Delete", [this] { DeleteSelectedBranch(); });
    update_button_ = MakeTextButton("Update", [this] { UpdateSelectedBranch(); });

    ftxui::InputOption rename_branch_option;
    rename_branch_option.multiline = false;
    rename_branch_input_component_ =
        ftxui::Input(&rename_branch_input_, "new branch name", rename_branch_option);

    ftxui::InputOption remote_filter_option;
    remote_filter_option.multiline = false;
    auto remote_filter_input =
        ftxui::Input(&remote_branch_filter_, "filter remote branches", remote_filter_option);
    remote_filter_input_component_ = ftxui::CatchEvent(
        remote_filter_input,
        [this, remote_filter_input](ftxui::Event event) {
            const bool handled = remote_filter_input->OnEvent(event);
            if (handled) {
                RebuildRemoteBranchLabels();
            }
            return handled;
        });

    ftxui::MenuOption remote_menu_option = ftxui::MenuOption::Vertical();
    remote_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
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
    remote_branch_menu_ = ftxui::Menu(&remote_branch_labels_, &selected_remote_branch_, remote_menu_option);
    refresh_remote_button_ = MakeTextButton("Refresh remote", [this] { RefreshRemoteBranches(); });
    checkout_remote_button_ = MakeTextButton("Checkout remote", [this] { CheckoutSelectedRemoteBranch(); });
    delete_remote_button_ = MakeTextButton("Delete remote", [this] { DeleteSelectedRemoteBranch(); });

    ftxui::InputOption tag_filter_option;
    tag_filter_option.multiline = false;
    auto tag_filter_input = ftxui::Input(&tag_filter_, "filter tags", tag_filter_option);
    tag_filter_input_component_ = ftxui::CatchEvent(
        tag_filter_input,
        [this, tag_filter_input](ftxui::Event event) {
            const bool handled = tag_filter_input->OnEvent(event);
            if (handled) {
                RebuildTagLabels();
            }
            return handled;
        });

    ftxui::MenuOption tag_menu_option = ftxui::MenuOption::Vertical();
    tag_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
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
    tag_menu_ = ftxui::Menu(&tag_labels_, &selected_tag_, tag_menu_option);

    ftxui::InputOption tag_name_option;
    tag_name_option.multiline = false;
    tag_name_input_component_ = ftxui::Input(&tag_name_input_, "tag name", tag_name_option);

    ftxui::InputOption tag_message_option;
    tag_message_option.multiline = false;
    tag_message_input_component_ = ftxui::Input(&tag_message_input_, "optional message", tag_message_option);

    create_tag_button_ = MakeTextButton("Create tag", [this] { CreateTag(); });
    delete_tag_button_ = MakeTextButton("Delete tag", [this] { DeleteSelectedTag(); });
    push_tag_button_ = MakeTextButton("Push tag", [this] { PushSelectedTag(); });
    push_all_tags_button_ = MakeTextButton("Push all tags", [this] { PushAllTags(); });
    fetch_tags_button_ = MakeTextButton("Fetch tags", [this] { FetchTags(); });

    ftxui::InputOption confirm_input_option;
    confirm_input_option.multiline = false;
    confirm_input_component_ = ftxui::Input(&confirm_typed_text_, "confirmation", confirm_input_option);
    confirm_confirm_button_ = MakeTextButton("Confirm", [this] { ConfirmPendingAction(); });
    confirm_cancel_button_ = MakeTextButton("Cancel", [this] { CancelPendingAction(); });
    confirm_container_ = ftxui::Container::Vertical({
        confirm_input_component_,
        ftxui::Container::Horizontal({
            confirm_confirm_button_,
            confirm_cancel_button_,
        }),
    });

    check_connection_button_ = MakeTextButton("Check connection", [this] { CheckConnection(); });
    fetch_button_ = MakeTextButton("Fetch", [this] { FetchServer(); });
    push_button_ = MakeTextButton("Push", [this] { PushServer(); });
    force_push_button_ = MakeTextButton("Force push lease", [this] { ForcePushWithLease(); });

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
            working_diff_button_,
            staged_diff_button_,
            refresh_diff_button_,
            copy_diff_button_,
        }),
    });

    commit_tab_container_ = ftxui::Container::Vertical({
        commit_file_menu_,
        commit_message_input_,
        commit_button_,
    });

    branches_tab_container_ = ftxui::Container::Vertical({
        branch_menu_,
        rename_branch_input_component_,
        ftxui::Container::Horizontal({
            checkout_button_,
            merge_button_,
            rebase_button_,
            rename_button_,
            delete_branch_button_,
            update_button_,
        }),
    });

    remote_tab_container_ = ftxui::Container::Vertical({
        remote_filter_input_component_,
        remote_branch_menu_,
        ftxui::Container::Horizontal({
            refresh_remote_button_,
            checkout_remote_button_,
            delete_remote_button_,
        }),
    });

    tags_tab_container_ = ftxui::Container::Vertical({
        tag_filter_input_component_,
        tag_menu_,
        tag_name_input_component_,
        tag_message_input_component_,
        ftxui::Container::Horizontal({
            create_tag_button_,
            delete_tag_button_,
            push_tag_button_,
            push_all_tags_button_,
            fetch_tags_button_,
        }),
    });

    server_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            check_connection_button_,
            fetch_button_,
            push_button_,
            force_push_button_,
        }),
    });

    tab_body_container_ = ftxui::Container::Tab({
        status_tab_container_,
        diff_tab_container_,
        commit_tab_container_,
        branches_tab_container_,
        remote_tab_container_,
        tags_tab_container_,
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
        } else if (selected_tab_ == static_cast<int>(Tab::Commit) && commit_file_menu_) {
            commit_file_menu_->TakeFocus();
        } else if (selected_tab_ == static_cast<int>(Tab::Remote) && remote_filter_input_component_) {
            remote_filter_input_component_->TakeFocus();
        } else if (selected_tab_ == static_cast<int>(Tab::Tags) && tag_filter_input_component_) {
            tag_filter_input_component_->TakeFocus();
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
    RefreshRemoteBranches();
    RefreshTags();
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
        remote_tab_button_->Render(),
        ftxui::text(" "),
        tags_tab_button_->Render(),
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
    } else if (selected_tab_ == static_cast<int>(Tab::Remote)) {
        body = RenderRemoteTab();
    } else if (selected_tab_ == static_cast<int>(Tab::Tags)) {
        body = RenderTagsTab();
    } else {
        body = RenderServerTab();
    }

    body = body |
        ftxui::bgcolor(theme.modal_background) |
        ftxui::color(theme.modal_foreground);

    if (!confirm_active_) {
        return body;
    }

    return ftxui::dbox({
        body,
        ftxui::vbox({
            ftxui::filler(),
            ftxui::hbox({
                ftxui::filler(),
                RenderConfirmOverlay(),
                ftxui::filler(),
            }),
            ftxui::filler(),
        }),
    });
}

std::string GitModalContent::GetFooterText() const {
    return status_.empty() ? "Git" : status_;
}

bool GitModalContent::HandleEvent(ftxui::Event event) {
    if (confirm_active_) {
        return HandleConfirmEvent(event);
    }

    if (event == ftxui::Event::Escape) {
        if (on_close_) {
            on_close_();
        }
        return true;
    }


    if (selected_tab_ == static_cast<int>(Tab::Diff)) {
        const bool wheel_down = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelDown;
        const bool wheel_up = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelUp;
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::PageDown || wheel_down) {
            diff_scroll_offset_ += event == ftxui::Event::PageDown ? 10 : 3;
            return true;
        }
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::PageUp || wheel_up) {
            diff_scroll_offset_ = std::max(0, diff_scroll_offset_ - (event == ftxui::Event::PageUp ? 10 : 3));
            return true;
        }
    }

    if (selected_tab_ == static_cast<int>(Tab::Server)) {
        const bool wheel_down = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelDown;
        const bool wheel_up = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelUp;
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::PageDown || wheel_down) {
            server_output_scroll_offset_ += event == ftxui::Event::PageDown ? 10 : 3;
            return true;
        }
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::PageUp || wheel_up) {
            server_output_scroll_offset_ = std::max(0, server_output_scroll_offset_ - (event == ftxui::Event::PageUp ? 10 : 3));
            return true;
        }
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
