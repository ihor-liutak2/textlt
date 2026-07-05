#include "modals/modal_git.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "ui_button.hpp"

namespace textlt {
namespace {

ButtonSpec GitButtonSpec(std::string label) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.variant = ButtonVariant::AccentBar;

    const std::string& caption = spec.caption;
    if (caption == "Commit" || caption == "Open" || caption == "Open side by side" ||
        caption == "Open unified diff" || caption == "Checkout" ||
        caption == "Checkout remote" || caption == "Create tag" || caption == "Push" ||
        caption == "Fetch") {
        spec.role = ButtonRole::Primary;
        return spec;
    }
    if (caption == "Confirm") {
        spec.role = ButtonRole::Primary;
        return spec;
    }
    if (caption == "Cancel") {
        spec.role = ButtonRole::Cancel;
        return spec;
    }
    if (caption.find("Delete") != std::string::npos ||
        caption.find("Force push") != std::string::npos) {
        spec.role = ButtonRole::Danger;
        return spec;
    }
    if (caption.find("Copy") != std::string::npos ||
        caption.find("Refresh") != std::string::npos ||
        caption == "Fetch tags" || caption == "Check connection") {
        spec.role = ButtonRole::Utility;
        return spec;
    }
    if (caption == "Working" || caption == "Staged") {
        spec.role = ButtonRole::Toggle;
        spec.size = ButtonSize::Compact;
        return spec;
    }
    if (caption.find("Unstage") != std::string::npos ||
        caption.find("Rebase") != std::string::npos ||
        caption.find("Merge") != std::string::npos ||
        caption.find("Rename") != std::string::npos ||
        caption.find("Update") != std::string::npos) {
        spec.role = ButtonRole::Warning;
        return spec;
    }
    if (caption.find("Stage") != std::string::npos ||
        caption == "Push tag" || caption == "Push all tags") {
        spec.role = ButtonRole::Success;
        return spec;
    }

    spec.role = ButtonRole::Secondary;
    return spec;
}

} // namespace

GitModalContent::GitModalContent(
    const Theme* theme,
    GitManager* git_manager,
    OpenFileCallback on_open_file,
    OpenCompareCallback on_open_compare,
    WriteClipboardCallback write_clipboard,
    CloseCallback on_close,
    RequestRedrawCallback request_redraw)
    : theme_(theme),
      git_manager_(git_manager),
      on_open_file_(std::move(on_open_file)),
      on_open_compare_(std::move(on_open_compare)),
      write_clipboard_(std::move(write_clipboard)),
      on_close_(std::move(on_close)),
      request_redraw_(std::move(request_redraw)) {
    status_tab_button_ = MakeTabButton("Status", static_cast<int>(Tab::Status));
    diff_tab_button_ = MakeTabButton("Diff", static_cast<int>(Tab::Diff));
    commit_tab_button_ = MakeTabButton("Commit", static_cast<int>(Tab::Commit));
    branches_tab_button_ = MakeTabButton("Branches", static_cast<int>(Tab::Branches));
    remote_tab_button_ = MakeTabButton("Remote", static_cast<int>(Tab::Remote));
    tags_tab_button_ = MakeTabButton("Tags", static_cast<int>(Tab::Tags));
    server_tab_button_ = MakeTabButton("Server", static_cast<int>(Tab::Server));
    compare_tab_button_ = MakeTabButton("Compare", static_cast<int>(Tab::Compare));

    tab_buttons_ = ftxui::Container::Horizontal({
        status_tab_button_,
        diff_tab_button_,
        commit_tab_button_,
        branches_tab_button_,
        remote_tab_button_,
        tags_tab_button_,
        server_tab_button_,
        compare_tab_button_,
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
    commit_message_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
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
    rename_branch_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    rename_branch_input_component_ =
        ftxui::Input(&rename_branch_input_, "new branch name", rename_branch_option);

    ftxui::InputOption remote_filter_option;
    remote_filter_option.multiline = false;
    remote_filter_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
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
    tag_filter_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
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
    tag_name_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    tag_name_input_component_ = ftxui::Input(&tag_name_input_, "tag name", tag_name_option);

    ftxui::InputOption tag_message_option;
    tag_message_option.multiline = false;
    tag_message_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    tag_message_input_component_ = ftxui::Input(&tag_message_input_, "optional message", tag_message_option);

    create_tag_button_ = MakeTextButton("Create tag", [this] { CreateTag(); });
    delete_tag_button_ = MakeTextButton("Delete tag", [this] { DeleteSelectedTag(); });
    push_tag_button_ = MakeTextButton("Push tag", [this] { PushSelectedTag(); });
    push_all_tags_button_ = MakeTextButton("Push all tags", [this] { PushAllTags(); });
    fetch_tags_button_ = MakeTextButton("Fetch tags", [this] { FetchTags(); });


    ftxui::MenuOption compare_ref_menu_option = ftxui::MenuOption::Vertical();
    compare_ref_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
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
    compare_left_ref_menu_ = ftxui::Menu(&compare_ref_labels_, &selected_compare_left_ref_, compare_ref_menu_option);
    compare_right_ref_menu_ = ftxui::Menu(&compare_ref_labels_, &selected_compare_right_ref_, compare_ref_menu_option);

    ftxui::MenuOption compare_file_menu_option = ftxui::MenuOption::Vertical();
    compare_file_menu_option.on_enter = [this] { OpenCompareSideBySide(); };
    compare_file_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
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
    compare_file_menu_ = ftxui::Menu(&compare_file_labels_, &selected_compare_file_, compare_file_menu_option);

    refresh_compare_refs_button_ = MakeTextButton("Refresh refs", [this] {
        RefreshCompareRefs();
        RefreshCompareFiles();
    });
    refresh_compare_files_button_ = MakeTextButton("Refresh files", [this] { RefreshCompareFiles(); });
    open_compare_side_button_ = MakeTextButton("Open side by side", [this] { OpenCompareSideBySide(); });
    open_compare_diff_button_ = MakeTextButton("Open unified diff", [this] { OpenCompareUnifiedDiff(); });
    copy_compare_diff_button_ = MakeTextButton("Copy diff", [this] { CopyCompareDiff(); });

    ftxui::InputOption confirm_input_option;
    confirm_input_option.multiline = false;
    confirm_input_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    confirm_input_component_ = ftxui::Input(&confirm_typed_text_, "confirmation", confirm_input_option);
    confirm_confirm_button_ = MakeTextButton("Confirm", [this] { ConfirmPendingAction(); });
    confirm_cancel_button_ = MakeTextButton("Cancel", [this] { CancelPendingAction(); });
    auto confirm_controls = ftxui::Container::Vertical({
        confirm_input_component_,
        ftxui::Container::Horizontal({
            confirm_confirm_button_,
            confirm_cancel_button_,
        }),
    });
    confirm_container_ = ftxui::CatchEvent(
        confirm_controls,
        [this](ftxui::Event event) {
            if (event == ftxui::Event::Escape) {
                CancelPendingAction();
                return true;
            }
            if (event == ftxui::Event::Return && confirm_required_text_.empty()) {
                ConfirmPendingAction();
                return true;
            }
            return false;
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

    compare_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            refresh_compare_refs_button_,
            refresh_compare_files_button_,
            open_compare_side_button_,
            open_compare_diff_button_,
            copy_compare_diff_button_,
        }),
        ftxui::Container::Horizontal({
            compare_left_ref_menu_,
            compare_right_ref_menu_,
        }),
        compare_file_menu_,
    });

    tab_body_container_ = ftxui::Container::Tab({
        status_tab_container_,
        diff_tab_container_,
        commit_tab_container_,
        branches_tab_container_,
        remote_tab_container_,
        tags_tab_container_,
        server_tab_container_,
        compare_tab_container_,
    }, &selected_tab_);

    auto primary_container = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
    primary_container = ftxui::CatchEvent(
        primary_container,
        [this](ftxui::Event event) { return HandleEvent(std::move(event)); });
    container_ = ftxui::Container::Tab(
        {primary_container, confirm_container_}, &confirm_layer_index_);
}

GitModalContent::~GitModalContent() {
    if (operation_thread_.joinable()) {
        operation_thread_.join();
    }
}

void GitModalContent::StartBackgroundOperation(
    BackgroundOperation operation,
    std::string action,
    std::function<GitManager::CommandResult()> command) {
    if (operation_running_ || !command) {
        return;
    }
    if (operation_thread_.joinable()) {
        operation_thread_.join();
    }

    background_operation_ = operation;
    operation_action_ = std::move(action);
    operation_frame_ = 0;
    operation_completed_ = false;
    operation_running_ = true;
    status_ = operation_action_ + " in progress...";
    if (request_redraw_) {
        request_redraw_();
    }

    operation_thread_ = std::thread([this, command = std::move(command)]() mutable {
        auto future = std::async(std::launch::async, [command = std::move(command)]() mutable {
            return command();
        });
        while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            ++operation_frame_;
            if (request_redraw_) {
                request_redraw_();
            }
        }

        GitManager::CommandResult result;
        try {
            result = future.get();
        } catch (const std::exception& error) {
            result.exit_code = -1;
            result.output = error.what();
        } catch (...) {
            result.exit_code = -1;
            result.output = "Unknown background Git error.";
        }
        {
            std::lock_guard<std::mutex> lock(operation_mutex_);
            operation_result_ = std::move(result);
            operation_completed_ = true;
        }
        if (request_redraw_) {
            request_redraw_();
        }
    });
}

void GitModalContent::ApplyBackgroundOperationCompletion() {
    GitManager::CommandResult result;
    {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        if (!operation_completed_) {
            return;
        }
        result = std::move(operation_result_);
        operation_completed_ = false;
    }

    operation_running_ = false;
    if (operation_thread_.joinable()) {
        operation_thread_.join();
    }

    if (background_operation_ == BackgroundOperation::Commit) {
        RunAndRefresh(operation_action_, result);
        if (result.success()) {
            commit_message_.clear();
        }
    } else {
        server_output_ = result.output;
        server_output_scroll_offset_ = 0;
        SplitOutputLines(server_output_, &server_output_lines_);
        if (background_operation_ == BackgroundOperation::CheckConnection) {
            status_ = result.success() ? "Connection OK." : "Connection check failed.";
        } else {
            RunAndRefresh(operation_action_, result);
        }
    }
    background_operation_ = BackgroundOperation::None;
}

ftxui::Element GitModalContent::RenderOperationOverlay() const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::vbox({
        ftxui::hbox({
            ftxui::spinner(0, operation_frame_.load()) | ftxui::bold,
            ftxui::text("  " + operation_action_ + "...") |
                ftxui::bold | ftxui::color(theme.modal_text_color),
        }),
        ftxui::text("Please wait. The Git operation is still running.") |
            ftxui::dim | ftxui::color(theme.modal_text_color),
    }) |
        ftxui::border |
        ftxui::bgcolor(theme.modal_background) |
        ftxui::color(theme.modal_border) |
        ftxui::clear_under;
}

ftxui::Component GitModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = GitButtonSpec(std::move(label));
    return MakeButton(theme_, std::move(spec), std::move(on_click));
}

ftxui::Component GitModalContent::MakeTabButton(std::string label, int tab_index) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.role = ButtonRole::Tab;
    spec.variant = ButtonVariant::AccentBar;
    spec.size = ButtonSize::Compact;

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
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
        } else if (selected_tab_ == static_cast<int>(Tab::Compare) && compare_file_menu_) {
            if (compare_refs_.empty()) {
                RefreshCompareRefs();
            }
            if (compare_entries_.empty()) {
                RefreshCompareFiles();
            }
            compare_file_menu_->TakeFocus();
        }
    };
    option.transform = [this, tab_index, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ButtonSpec resolved_spec = spec;
        resolved_spec.selected = selected_tab_ == tab_index;
        ftxui::Element tab = RenderButton(theme, resolved_spec, state.focused || state.active);
        if (resolved_spec.selected || state.focused || state.active) {
            tab |= ftxui::bold;
        } else {
            tab |= ftxui::dim;
        }
        return tab;
    };
    return ftxui::Button(option);
}

void GitModalContent::Open() {
    confirm_active_ = false;
    confirm_layer_index_ = 0;
    selected_tab_ = static_cast<int>(Tab::Status);
    status_ = "Ready";
    server_output_.clear();
    server_output_lines_.clear();
    RefreshAll();
    RefreshRemoteBranches();
    RefreshTags();
    RefreshCompareRefs();
    RefreshCompareFiles();
    if (status_list_component_) {
        status_list_component_->TakeFocus();
    }
}

void GitModalContent::Close() {
    confirm_active_ = false;
    confirm_layer_index_ = 0;
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
        ftxui::text(" "),
        compare_tab_button_->Render(),
    });
}

ftxui::Element GitModalContent::Render() {
    ApplyBackgroundOperationCompletion();
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
    } else if (selected_tab_ == static_cast<int>(Tab::Compare)) {
        body = RenderCompareTab();
    } else {
        body = RenderServerTab();
    }

    body = body |
        ftxui::bgcolor(theme.modal_background) |
        ftxui::color(theme.modal_foreground);

    if (!confirm_active_ && !operation_running_) {
        return body;
    }

    ftxui::Element overlay = operation_running_
        ? RenderOperationOverlay()
        : RenderConfirmOverlay();

    return ftxui::dbox({
        body,
        ftxui::vbox({
            ftxui::filler(),
            ftxui::hbox({
                ftxui::filler(),
                overlay,
                ftxui::filler(),
            }),
            ftxui::filler(),
        }),
    });
}

std::string GitModalContent::GetFooterText() const {
    return status_.empty() ? "Git" : status_;
}

ftxui::Element GitModalContent::RenderCustomFooter() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ftxui::Elements actions;
    const auto add = [&actions](const ftxui::Component& button) {
        if (!actions.empty()) {
            actions.push_back(ftxui::text(" "));
        }
        actions.push_back(button->Render());
    };

    const Tab tab = static_cast<Tab>(selected_tab_);
    if (tab == Tab::Status) {
        add(refresh_button_); add(open_button_); add(stage_all_button_);
        add(unstage_all_button_); add(copy_paths_button_);
    } else if (tab == Tab::Diff) {
        add(working_diff_button_); add(staged_diff_button_);
        add(refresh_diff_button_); add(copy_diff_button_);
    } else if (tab == Tab::Commit) {
        add(commit_button_);
    } else if (tab == Tab::Branches) {
        add(checkout_button_); add(merge_button_); add(rebase_button_);
        add(rename_button_); add(delete_branch_button_); add(update_button_);
    } else if (tab == Tab::Remote) {
        add(refresh_remote_button_); add(checkout_remote_button_); add(delete_remote_button_);
    } else if (tab == Tab::Tags) {
        add(create_tag_button_); add(delete_tag_button_); add(push_tag_button_);
        add(push_all_tags_button_); add(fetch_tags_button_);
    } else if (tab == Tab::Server) {
        add(check_connection_button_); add(fetch_button_); add(push_button_); add(force_push_button_);
    } else if (tab == Tab::Compare) {
        add(refresh_compare_refs_button_); add(refresh_compare_files_button_);
        add(open_compare_side_button_); add(open_compare_diff_button_); add(copy_compare_diff_button_);
    }

    return ftxui::vbox({
        ftxui::text(" " + GetFooterText()) | ftxui::dim | ftxui::color(theme.modal_text_color),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox(std::move(actions)),
    });
}

bool GitModalContent::HandleEvent(ftxui::Event event) {
    ApplyBackgroundOperationCompletion();
    if (operation_running_) {
        return true;
    }
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

    if (selected_tab_ == static_cast<int>(Tab::Compare)) {
        const bool wheel_down = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelDown;
        const bool wheel_up = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelUp;
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::PageDown || wheel_down) {
            compare_diff_scroll_offset_ += event == ftxui::Event::PageDown ? 10 : 3;
            return false;
        }
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::PageUp || wheel_up) {
            compare_diff_scroll_offset_ = std::max(0, compare_diff_scroll_offset_ - (event == ftxui::Event::PageUp ? 10 : 3));
            return false;
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
    OpenCompareCallback on_open_compare,
    WriteClipboardCallback write_clipboard,
    GitModalContent::RequestRedrawCallback request_redraw)
    : theme_(theme) {
    content_ = std::make_shared<GitModalContent>(
        theme_,
        git_manager,
        std::move(on_open_file),
        std::move(on_open_compare),
        std::move(write_clipboard),
        [this] { Close(); },
        std::move(request_redraw));
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
