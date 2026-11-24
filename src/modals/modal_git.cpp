#include "modals/modal_git.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <sstream>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/string.hpp"

#include "editor_utils.hpp"
#include "ui_button.hpp"
#include "ui_horizontal_scroll.hpp"

namespace textlt {
namespace {

// The original content determines column sizing. Clip rows only after the layout assigns a width.
class MeasuredLogPane final : public ftxui::Node {
public:
    MeasuredLogPane(int preferred_width, std::function<ftxui::Element(int)> render)
        : preferred_width_(preferred_width), render_(std::move(render)) {}

    void ComputeRequirement() override { requirement_.min_x = preferred_width_; }

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        const int width = std::max(0, box.x_max - box.x_min + 1);
        if (width != width_) {
            width_ = width;
            children_ = {render_(width)};
            children_.front()->ComputeRequirement();
        }
        children_.front()->SetBox(box);
    }

private:
    int preferred_width_;
    int width_ = -1;
    std::function<ftxui::Element(int)> render_;
};

std::string LogTextSlice(const std::string& text, int offset, int width) {
    const auto start = utils::Utf8ByteIndexAtDisplayColumn(text, 0, offset);
    auto end = utils::Utf8ByteIndexAtDisplayColumn(text, start, width);
    if (!offset || start == text.size() || utils::Utf8DisplayWidth(text, 0, start) == static_cast<size_t>(offset)) {
        return text.substr(start, end - start);
    }
    // Keep the remaining cells of a clipped wide glyph aligned with other rows.
    const auto next = utils::NextUtf8CodepointStart(text, start);
    const auto padding = std::min(static_cast<size_t>(width), utils::Utf8DisplayWidth(text, 0, next) - offset);
    end = utils::Utf8ByteIndexAtDisplayColumn(text, next, width - padding);
    return std::string(padding, ' ') + text.substr(next, end - next);
}

ButtonSpec GitButtonSpec(std::string label) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.variant = ButtonVariant::AccentEdges;

    const std::string& caption = spec.caption;
    if (caption == "Cancel") {
        spec.role = ButtonRole::Cancel;
        return spec;
    }
    if (caption == "Working" || caption == "Staged") {
        spec.role = ButtonRole::Toggle;
        spec.size = ButtonSize::Compact;
        return spec;
    }
    if (caption.find("Merge") != std::string::npos ||
        caption.find("Rebase") != std::string::npos ||
        caption.find("Move") != std::string::npos ||
        caption.find("Delete") != std::string::npos ||
        caption.find("Update") != std::string::npos ||
        caption.find("Rename") != std::string::npos ||
        caption == "Push" || caption == "Push tag" ||
        caption == "Push all tags" || caption.find("Force push") != std::string::npos) {
        spec.role = ButtonRole::Warning;
        return spec;
    }
    if (caption.find("Copy") != std::string::npos ||
        caption.find("Refresh") != std::string::npos ||
        caption == "Check connection") {
        spec.role = ButtonRole::Utility;
        return spec;
    }

    spec.role = ButtonRole::Primary;
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
    logs_tab_button_ = MakeTabButton("Logs", static_cast<int>(Tab::Logs));
    diff_tab_button_ = MakeTabButton("Diff", static_cast<int>(Tab::Diff));
    commit_tab_button_ = MakeTabButton("Commit", static_cast<int>(Tab::Commit));
    branches_tab_button_ = MakeTabButton("Branches", static_cast<int>(Tab::Branches));
    remote_tab_button_ = MakeTabButton("Remote", static_cast<int>(Tab::Remote));
    tags_tab_button_ = MakeTabButton("Tags", static_cast<int>(Tab::Tags));
    server_tab_button_ = MakeTabButton("Server", static_cast<int>(Tab::Server));
    compare_tab_button_ = MakeTabButton("Compare", static_cast<int>(Tab::Compare));

    tab_buttons_ = ftxui::Container::Horizontal({
        status_tab_button_,
        logs_tab_button_,
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
    commit_message_input_ = ClipboardController::MakeInput(&commit_message_, "commit message", commit_message_option);
    commit_button_ = MakeTextButton("Commit", [this] { CommitStagedFiles(); });
    copy_commit_output_button_ = MakeTextButton("Copy output", [this] { CopyCommitOutput(); });

    ftxui::MenuOption branch_menu_option = ftxui::MenuOption::Vertical();
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
    new_branch_button_ = MakeTextButton("New", [this] { CreateNewBranch(); });
    update_button_ = MakeTextButton("Update", [this] { UpdateSelectedBranch(); });
    copy_branch_output_button_ = MakeTextButton("Copy output", [this] { CopyBranchOutput(); });

    ftxui::InputOption popup_input_option;
    popup_input_option.multiline = false;
    popup_input_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element | ftxui::bgcolor(theme.modal_background) |
            ftxui::color(theme.modal_foreground);
    };
    branch_input_component_ =
        ClipboardController::MakeInput(&branch_input_value_, "", popup_input_option);
    branch_input_confirm_button_ = MakeTextButton("Confirm", [this] { ConfirmBranchInput(); });
    branch_input_cancel_button_ = MakeTextButton("Cancel", [this] { CancelBranchInput(); });
    branch_input_container_ = ftxui::CatchEvent(
        ftxui::Container::Vertical({
            branch_input_component_,
            ftxui::Container::Horizontal({
                branch_input_confirm_button_,
                branch_input_cancel_button_,
            }),
        }),
        [this](ftxui::Event event) {
            if (event == ftxui::Event::Escape) {
                CancelBranchInput();
                return true;
            }
            if (event == ftxui::Event::Return) {
                ConfirmBranchInput();
                return true;
            }
            return false;
        });

    ftxui::InputOption remote_filter_option;
    remote_filter_option.multiline = false;
    remote_filter_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    auto remote_filter_input =
        ClipboardController::MakeInput(&remote_branch_filter_, "filter remote branches", remote_filter_option);
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
    auto tag_filter_input = ClipboardController::MakeInput(&tag_filter_, "filter tags", tag_filter_option);
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
    tag_name_input_component_ = ClipboardController::MakeInput(&tag_name_input_, "tag name", tag_name_option);

    ftxui::InputOption tag_message_option;
    tag_message_option.multiline = false;
    tag_message_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    tag_message_input_component_ = ClipboardController::MakeInput(&tag_message_input_, "optional message", tag_message_option);

    create_tag_button_ = MakeTextButton("Create tag", [this] { CreateTag(); });
    move_tag_button_ = MakeTextButton("Move to HEAD", [this] { MoveSelectedTag(); });
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

    confirm_input_component_ = ClipboardController::MakeInput(&confirm_typed_text_, "confirmation", popup_input_option);
    auto checkbox_option = ftxui::CheckboxOption::Simple();
    checkbox_option.transform = [](const ftxui::EntryState& state) {
        auto label = ftxui::text(state.label);
        if (state.active) {
            label |= ftxui::bold;
        }
        if (state.focused) {
            label |= ftxui::inverted;
        }
        return ftxui::hbox({ftxui::text(state.state ? "[X] " : "[ ] "), label});
    };
    confirm_force_tag_checkbox_ = ftxui::Checkbox(
        "Force push (overwrite remote tag)", &confirm_force_tag_push_, std::move(checkbox_option));
    confirm_confirm_button_ = MakeTextButton("Confirm", [this] { ConfirmPendingAction(); });
    confirm_cancel_button_ = MakeTextButton("Cancel", [this] { CancelPendingAction(); });
    auto confirm_controls = ftxui::Container::Vertical({
        confirm_input_component_,
        confirm_force_tag_checkbox_,
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
    copy_server_output_button_ = MakeTextButton("Copy output", [this] { CopyServerOutput(); });

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
        ftxui::Container::Horizontal({
            commit_button_,
            copy_commit_output_button_,
        }),
    });

    branches_tab_container_ = ftxui::Container::Vertical({
        branch_menu_,
        ftxui::Container::Horizontal({
            checkout_button_,
            merge_button_,
            rebase_button_,
            rename_button_,
            delete_branch_button_,
            update_button_,
            new_branch_button_,
            copy_branch_output_button_,
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
            move_tag_button_,
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
            copy_server_output_button_,
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

    // --- Logs tab components ---
    ftxui::MenuOption log_branch_menu_option = ftxui::MenuOption::Vertical();
    log_branch_menu_option.on_enter = [this] { SelectLogBranch(); };
    log_branch_menu_option.on_change = [this] {
        // Auto-load commits when branch changes via keyboard navigation.
        SelectLogBranch();
    };
    log_branch_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(LogTextSlice(state.label, log_branch_scroll_.offset,
                                                      log_branch_scroll_.width));
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    log_branch_menu_ = ftxui::Menu(&log_branch_labels_, &log_selected_branch_, log_branch_menu_option);
    log_branch_component_ = ftxui::CatchEvent(
        log_branch_menu_,
        [this](ftxui::Event event) {
            if (event == ftxui::Event::Character(" ")) {
                SelectLogBranch();
                return true;
            }
            return false;
        });

    ftxui::MenuOption log_commit_menu_option = ftxui::MenuOption::Vertical();
    log_commit_menu_option.on_enter = [this] { SelectLogCommit(); };
    log_commit_menu_option.on_change = [this] {
        SelectLogCommit();
    };
    log_commit_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(LogTextSlice(state.label, log_commit_scroll_.offset,
                                                      log_commit_scroll_.width));
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    log_commit_menu_ = ftxui::Menu(&log_commit_labels_, &log_selected_commit_, log_commit_menu_option);
    log_commit_component_ = ftxui::CatchEvent(
        log_commit_menu_,
        [this](ftxui::Event event) {
            if (event == ftxui::Event::Character(" ")) {
                SelectLogCommit();
                return true;
            }
            return false;
        });

    log_local_button_ = MakeTextButton("Local", [this] {
        log_remote_mode_ = false;
        RefreshLogs();
    });
    log_remote_button_ = MakeTextButton("Remote", [this] {
        log_remote_mode_ = true;
        RefreshLogs();
    });
    log_refresh_button_ = MakeTextButton("Refresh", [this] {
        RefreshLogs();
    });
    log_copy_details_button_ = MakeTextButton("Copy details", [this] {
        CopyLogCommitDetails();
    });

    logs_tab_container_ = ftxui::Container::Horizontal({
        ftxui::Container::Vertical({
            log_branch_component_,
        }),
        ftxui::Container::Vertical({
            log_commit_component_,
        }),
        // These controls are rendered in the custom footer. They must also be
        // present in the active component tree so FTXUI can route mouse clicks
        // to the footer buttons.
        ftxui::Container::Horizontal({
            log_local_button_,
            log_remote_button_,
            log_refresh_button_,
            log_copy_details_button_,
        }),
    });

    tab_body_container_ = ftxui::Container::Tab({
        status_tab_container_,
        logs_tab_container_,
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
        {primary_container, confirm_container_, branch_input_container_}, &confirm_layer_index_);
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
        commit_output_ = result.output;
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
    });
}

ftxui::Component GitModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = GitButtonSpec(std::move(label));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

ftxui::Component GitModalContent::MakeTabButton(std::string label, int tab_index) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = "  " + label + "  ";
    option.on_click = [this, tab_index] {
        selected_tab_ = tab_index;
        FocusTabBody(tab_index);
    };
    option.transform = [this, tab_index, label = std::move(label)](
        const ftxui::EntryState&) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalTabButton(
            theme,
            label,
            selected_tab_ == tab_index,
            false);
    };
    return ftxui::Button(option);
}

void GitModalContent::FocusTabBody(int tab_index) {
    if (tab_index == static_cast<int>(Tab::Status) && status_list_component_) {
        status_list_component_->TakeFocus();
    } else if (tab_index == static_cast<int>(Tab::Logs) && log_branch_component_) {
        log_branch_component_->TakeFocus();
    } else if (tab_index == static_cast<int>(Tab::Branches) && branch_menu_) {
        branch_menu_->TakeFocus();
    } else if (tab_index == static_cast<int>(Tab::Commit) && commit_file_menu_) {
        commit_file_menu_->TakeFocus();
    } else if (tab_index == static_cast<int>(Tab::Remote) && remote_filter_input_component_) {
        remote_filter_input_component_->TakeFocus();
    } else if (tab_index == static_cast<int>(Tab::Tags) && tag_filter_input_component_) {
        if (!tag_labels_.empty()) {
            selected_tag_ = static_cast<int>(tag_labels_.size()) - 1;
        }
        tag_filter_input_component_->TakeFocus();
    } else if (tab_index == static_cast<int>(Tab::Compare) && compare_file_menu_) {
        if (compare_refs_.empty()) {
            RefreshCompareRefs();
        }
        if (compare_entries_.empty()) {
            RefreshCompareFiles();
        }
        compare_file_menu_->TakeFocus();
    }
}

void GitModalContent::Open() {
    OpenAtTab(static_cast<int>(Tab::Status));
}

void GitModalContent::OpenBranches() {
    OpenAtTab(static_cast<int>(Tab::Branches));
}

void GitModalContent::OpenAtTab(int tab_index) {
    confirm_active_ = false;
    branch_input_active_ = false;
    branch_input_callback_ = nullptr;
    confirm_layer_index_ = 0;
    if (tab_buttons_ && tab_buttons_->ChildCount() > 0) {
        tab_index = std::clamp(
            tab_index, 0, static_cast<int>(tab_buttons_->ChildCount()) - 1);
    } else {
        tab_index = 0;
    }
    selected_tab_ = tab_index;
    status_ = "Ready";
    server_output_.clear();
    server_output_lines_.clear();
    commit_output_.clear();
    RefreshAll();
    RefreshRemoteBranches();
    RefreshTags();
    RefreshCompareRefs();
    RefreshCompareFiles();
    RefreshLogs();
    // The tab button container keeps the child selected by the last click even
    // when focus moves to the tab body. Re-point it at the active tab so only
    // that tab renders as active after reopening the modal.
    if (tab_buttons_ && tab_buttons_->ChildCount() > 0) {
        tab_buttons_->SetActiveChild(
            tab_buttons_->ChildAt(static_cast<size_t>(tab_index)));
    }
    FocusTabBody(tab_index);
}

void GitModalContent::Close() {
    confirm_active_ = false;
    branch_input_active_ = false;
    branch_input_callback_ = nullptr;
    confirm_layer_index_ = 0;
}

ftxui::Element GitModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

ftxui::Element GitModalContent::RenderHeaderRow() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return hbox({
        status_tab_button_->Render(),
        text(" "),
        logs_tab_button_->Render(),
        text(" "),
        diff_tab_button_->Render(),
        text(" "),
        commit_tab_button_->Render(),
        text(" "),
        branches_tab_button_->Render(),
        text(" "),
        remote_tab_button_->Render(),
        text(" "),
        tags_tab_button_->Render(),
        text(" "),
        server_tab_button_->Render(),
        text(" "),
        compare_tab_button_->Render(),
        filler(),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

ftxui::Element GitModalContent::Render() {
    ApplyBackgroundOperationCompletion();
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Element body;
    if (selected_tab_ == static_cast<int>(Tab::Status)) {
        body = RenderStatusTab();
    } else if (selected_tab_ == static_cast<int>(Tab::Logs)) {
        body = RenderLogsTab();
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

    body = ftxui::vbox({
        RenderHeaderRow(),
        ftxui::separator() | ftxui::color(theme.modal_border),
        body | ftxui::flex,
    }) |
        ftxui::bgcolor(theme.modal_background) |
        ftxui::color(theme.modal_foreground);

    if (!confirm_active_ && !branch_input_active_ && !operation_running_) {
        return body;
    }

    ftxui::Element overlay = operation_running_
        ? RenderOperationOverlay()
        : (branch_input_active_ ? RenderBranchInputOverlay() : RenderConfirmOverlay());
    overlay = overlay |
        ftxui::borderEmpty |
        ftxui::borderStyled(ftxui::LIGHT, theme.modal_border) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg) |
        ftxui::clear_under;

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
        add(commit_button_); add(copy_commit_output_button_);
    } else if (tab == Tab::Branches) {
        // Branch action buttons are rendered in the tab body (branches_tab_container_)
        // so they are proper clickable components. Footer keeps the copy action.
        add(copy_branch_output_button_);
    } else if (tab == Tab::Remote) {
        add(refresh_remote_button_); add(checkout_remote_button_); add(delete_remote_button_);
    } else if (tab == Tab::Tags) {
        add(create_tag_button_); add(move_tag_button_); add(delete_tag_button_);
        add(push_tag_button_); add(push_all_tags_button_); add(fetch_tags_button_);
    } else if (tab == Tab::Server) {
        add(check_connection_button_); add(fetch_button_); add(push_button_); add(force_push_button_);
        add(copy_server_output_button_);
    } else if (tab == Tab::Compare) {
        add(refresh_compare_refs_button_); add(refresh_compare_files_button_);
        add(open_compare_side_button_); add(open_compare_diff_button_); add(copy_compare_diff_button_);
    } else if (tab == Tab::Logs) {
        add(log_local_button_); add(log_remote_button_); add(log_refresh_button_);
        add(log_copy_details_button_);
    }

    return ftxui::vbox({
        ftxui::text(" " + GetFooterText()) | ftxui::dim | ftxui::color(theme.modal_text_color),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox(std::move(actions)),
    });
}

// --- Logs tab implementation ---

std::string GitModalContent::SelectedLogBranch() const {
    if (log_selected_branch_ < 0 ||
        log_selected_branch_ >= static_cast<int>(log_branches_.size())) {
        return "";
    }
    return log_branches_[static_cast<size_t>(log_selected_branch_)];
}

std::string GitModalContent::SelectedLogCommitHash() const {
    if (log_selected_commit_ < 0 ||
        log_selected_commit_ >= static_cast<int>(log_commits_.size())) {
        return "";
    }
    return log_commits_[static_cast<size_t>(log_selected_commit_)].full_hash;
}

void GitModalContent::SelectLogBranch() {
    const std::string branch = SelectedLogBranch();
    if (branch.empty()) {
        return;
    }
    log_commits_ = git_manager_->GetBranchCommits(branch);
    log_commit_scroll_.offset = 0;
    RebuildLogCommitLabels();
    log_selected_commit_ = 0;
    if (!log_commits_.empty()) {
        SelectLogCommit();
    } else {
        log_has_commit_detail_ = false;
    }
    log_status_ = log_commits_.empty()
        ? "No commits on branch " + branch + "."
        : std::to_string(log_commits_.size()) + " commit(s) on " + branch;
}

void GitModalContent::SelectLogCommit() {
    const std::string hash = SelectedLogCommitHash();
    if (hash.empty()) {
        log_has_commit_detail_ = false;
        return;
    }
    if (!log_has_commit_detail_ || log_commit_detail_.full_hash != hash) {
        log_details_scroll_.offset = 0;
        log_details_row_ = 0;
    }
    log_commit_detail_ = git_manager_->GetCommitDetail(hash);
    log_has_commit_detail_ = true;
    log_status_ = "Commit " + log_commit_detail_.full_hash.substr(0, 7);
}

void GitModalContent::CopyLogCommitDetails() {
    if (!write_clipboard_) {
        status_ = "Clipboard write is not configured.";
        return;
    }
    if (!log_has_commit_detail_) {
        status_ = "No commit details to copy.";
        return;
    }

    const GitManager::CommitDetail& detail = log_commit_detail_;
    std::string text = "Commit: " + detail.full_hash + "\n";
    text += "Author: " + detail.author_name + " <" + detail.author_email + ">\n";
    text += "Date: " + detail.date + "\n\n";
    text += "Message:\n" + detail.full_message;
    if (!detail.changed_files.empty()) {
        text += "\n\nChanged files:";
        for (const std::string& file : detail.changed_files) {
            text += "\n  " + file;
        }
    }

    status_ = write_clipboard_(text)
        ? "Commit details copied." : ClipboardController::kWriteFailed;
}

void GitModalContent::RebuildLogBranchLabels() {
    log_branch_labels_.clear();
    log_branch_labels_.reserve(log_branches_.size());
    for (size_t index = 0; index < log_branches_.size(); ++index) {
        const std::string& branch = log_branches_[index];
        std::string marker = (log_selected_branch_ == static_cast<int>(index)) ? "> " : "  ";
        log_branch_labels_.push_back(marker + branch);
    }
    if (log_selected_branch_ >= static_cast<int>(log_branch_labels_.size())) {
        log_selected_branch_ = static_cast<int>(log_branch_labels_.size()) - 1;
    }
    if (log_selected_branch_ < 0) {
        log_selected_branch_ = 0;
    }
}

void GitModalContent::RebuildLogCommitLabels() {
    log_commit_labels_.clear();
    log_commit_labels_.reserve(log_commits_.size());
    for (const auto& commit : log_commits_) {
        std::string label = commit.hash + "  " + commit.subject;
        if (!commit.author.empty()) {
            label += "  (" + commit.author + ")";
        }
        if (!commit.date.empty()) {
            label += "  " + commit.date;
        }
        log_commit_labels_.push_back(label);
    }
    if (log_selected_commit_ >= static_cast<int>(log_commit_labels_.size())) {
        log_selected_commit_ = static_cast<int>(log_commit_labels_.size()) - 1;
    }
    if (log_selected_commit_ < 0) {
        log_selected_commit_ = 0;
    }
}

void GitModalContent::RefreshLogs() {
    if (!git_manager_) {
        log_status_ = "Git manager is not configured.";
        return;
    }

    log_branches_.clear();
    if (log_remote_mode_) {
        log_branches_ = git_manager_->GetRemoteBranches();
    } else {
        std::string current;
        log_branches_ = git_manager_->GetLocalBranches(&current);
    }

    log_branch_scroll_.offset = 0;
    RebuildLogBranchLabels();
    log_commits_.clear();
    RebuildLogCommitLabels();
    log_selected_commit_ = 0;
    log_has_commit_detail_ = false;

    if (log_branches_.empty()) {
        log_status_ = log_remote_mode_ ? "No remote branches found." : "No local branches found.";
        status_ = log_status_;
        return;
    }

    // Auto-select the first branch and load its commits.
    log_selected_branch_ = 0;
    SelectLogBranch();
    status_ = (log_remote_mode_ ? "Remote" : "Local") +
        std::string(" logs refreshed: ") + std::to_string(log_branches_.size()) +
        " branch(es).";
}

void GitModalContent::LogHorizontalScroll::Update(int viewport_width, int content_width) {
    width = viewport_width;
    limit = width > 0 ? std::max(0, content_width - width) : 0;
    offset = std::min(offset, limit);
    if (limit == 0) {
        grab.reset();
        return;
    }
    thumb_width = std::max(1, width * width / content_width);
    thumb_start = static_cast<int>(static_cast<long long>(offset) * (width - thumb_width) / limit);
}

ftxui::Element GitModalContent::LogHorizontalScroll::Render(const Theme& theme) {
    using namespace ftxui;
    if (limit == 0) return emptyElement();
    const auto rule = [](std::string_view glyph, int count) {
        std::string line;
        for (int i = 0; i < count; ++i) line += glyph;
        return text(std::move(line));
    };
    auto scrollbar = hbox({
        rule("─", thumb_start) | color(theme.modal_border),
        rule("━", thumb_width) | color(theme.modal_accent),
        rule("─", width - thumb_start - thumb_width) | color(theme.modal_border),
    }) | size(WIDTH, EQUAL, width) | reflect(track);
    return hbox({std::move(scrollbar), filler()}) | size(HEIGHT, EQUAL, 1);
}

bool GitModalContent::LogHorizontalScroll::HandleMouse(const ftxui::Mouse& mouse) {
    using namespace ftxui;
    if (limit == 0) return false;
    if (mouse.motion == Mouse::Released && grab) {
        grab.reset();
        return true;
    }
    const bool pressed = mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed &&
        track.Contain(mouse.x, mouse.y);
    if (!pressed && !(grab && mouse.motion == Mouse::Moved)) return false;
    const int travel = width - thumb_width;
    if (travel <= 0) return true;
    const int column = mouse.x - track.x_min;
    if (pressed) {
        const int inside_thumb = column - thumb_start;
        if (inside_thumb >= 0 && inside_thumb < thumb_width) {
            grab = inside_thumb;
            return true;
        }
        grab = thumb_width / 2;
    }
    const int start = std::clamp(column - *grab, 0, travel);
    offset = static_cast<int>((static_cast<long long>(start) * limit + travel / 2) / travel);
    return true;
}

ftxui::Element GitModalContent::RenderLogsTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const auto header = [&](std::string title) { return text(std::move(title)) | bold | color(theme.modal_accent); };
    const auto divider = [&] { return separator() | color(theme.modal_border); };
    const auto list_pane = [&](std::string title, std::string empty, const Component& menu,
                               const std::vector<std::string>& labels, LogHorizontalScroll& scroll) {
        int longest = 0;
        for (const auto& label : labels) longest = std::max(longest, string_width(label));
        const int preferred_width = std::max(string_width(title),
            labels.empty() ? string_width(empty) : longest + 1);
        return std::make_shared<MeasuredLogPane>(preferred_width,
            [title = std::move(title), empty = std::move(empty), menu, longest,
             header, divider, &scroll, &theme, has_rows = !labels.empty()](int width) {
                scroll.Update(std::max(0, width - 1), longest);
                return vbox({
                    header(title),
                    divider(),
                    (has_rows ? menu->Render() | vscroll_indicator : text(empty) | color(theme.modal_text_color)) |
                        yframe | yflex,
                    scroll.Render(theme),
                });
            });
    };
    Element branches = list_pane(std::string(log_remote_mode_ ? "Remote branches (" : "Local branches (") +
        std::to_string(log_branches_.size()) + ")", "None.", log_branch_component_,
        log_branch_labels_, log_branch_scroll_);
    Element commits = list_pane("Commits", "No commits.", log_commit_component_, log_commit_labels_, log_commit_scroll_);

    std::vector<std::pair<std::string, bool>> details;
    int details_preferred_width = string_width("Commit details");
    const auto add_detail = [&](std::string line, bool heading = false) {
        details_preferred_width = std::max(details_preferred_width, string_width(line));
        details.emplace_back(std::move(line), heading);
    };
    if (!log_has_commit_detail_) {
        add_detail("Select a commit to view details.");
    } else {
        const auto& detail = log_commit_detail_;
        add_detail("Hash: " + detail.full_hash);
        add_detail("Author: " + detail.author_name + " <" + detail.author_email + ">");
        add_detail("Date: " + detail.date);
        add_detail("");
        add_detail("Message:", true);
        auto wrapped_message = paragraph(detail.full_message);
        wrapped_message->ComputeRequirement();
        details_preferred_width = std::max(details_preferred_width, wrapped_message->requirement().min_x);
        std::istringstream message(detail.full_message);
        for (std::string line; std::getline(message, line);) details.emplace_back(std::move(line), false);
        add_detail("");
        add_detail("Changed files:", true);
        if (detail.changed_files.empty()) add_detail("  (none)");
        for (const auto& file : detail.changed_files) add_detail("  " + file);
    }
    int longest_detail = 0;
    if (log_has_commit_detail_) {
        for (const auto& [line, heading] : details) longest_detail = std::max(longest_detail, string_width(line));
    }
    Element detail_pane = std::make_shared<MeasuredLogPane>(details_preferred_width,
        [this, details = std::move(details), longest_detail, header, divider, &theme](int width) {
            log_details_scroll_.Update(std::max(0, width - 1), longest_detail);
            log_details_row_ = std::clamp(log_details_row_, 0, static_cast<int>(details.size()) - 1);
            Elements rows;
            for (const auto& [line, heading] : details) {
                auto label = LogTextSlice(line, log_details_scroll_.offset, log_details_scroll_.width);
                rows.push_back(heading ? header(std::move(label)) : text(std::move(label)) | color(theme.modal_text_color));
            }
            return vbox({
                header("Commit details"),
                divider(),
                vbox(std::move(rows)) | focusPosition(0, log_details_row_) | vscroll_indicator | yframe | yflex |
                    reflect(log_details_box_),
                log_details_scroll_.Render(theme),
            });
        });

    if (!log_remote_mode_) {
        Box branch_box, commit_box, detail_box;
        const auto measure = [](const Element& pane, Box& box) {
            pane->ComputeRequirement();
            return emptyElement() | size(WIDTH, EQUAL, pane->requirement().min_x) | xflex | reflect(box);
        };
        auto natural = hbox({measure(branches, branch_box), divider(), measure(commits, commit_box),
                             divider(), measure(detail_pane, detail_box)});
        natural->ComputeRequirement();
        natural->SetBox({0, GetModalSizePreference().width - 3, 0, 0});

        const int commit_room = std::max(0, BoxWidth(commit_box) - string_width("Commits"));
        const int detail_room = std::max(0, BoxWidth(detail_box) - string_width("Commit details"));
        const int extra = std::min(15, commit_room + detail_room);
        const int from_commits = std::clamp((extra + 1) / 2, std::max(0, extra - detail_room),
                                            std::min(extra, commit_room));
        branches = branches | size(WIDTH, EQUAL, BoxWidth(branch_box) + extra);
        commits = commits | size(WIDTH, EQUAL, BoxWidth(commit_box) - from_commits);
        detail_pane = detail_pane | size(WIDTH, EQUAL, BoxWidth(detail_box) - (extra - from_commits));
    }
    return hbox({branches | xflex, divider(), commits | xflex, divider(), detail_pane | xflex}) |
        flex | bgcolor(theme.modal_background) | color(theme.modal_foreground);
}

bool GitModalContent::HandleEvent(ftxui::Event event) {
    ApplyBackgroundOperationCompletion();
    if (operation_running_) {
        return true;
    }
    if (branch_input_active_) {
        return HandleBranchInputEvent(event);
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


    if (selected_tab_ == static_cast<int>(Tab::Logs) && event.is_mouse()) {
        const auto mouse = event.mouse();
        if (log_branch_scroll_.HandleMouse(mouse) || log_commit_scroll_.HandleMouse(mouse) ||
            log_details_scroll_.HandleMouse(mouse)) return true;
        if (log_details_box_.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::WheelDown) {
                log_details_row_ += 3;
                return true;
            }
            if (mouse.button == ftxui::Mouse::WheelUp) {
                log_details_row_ = std::max(0, log_details_row_ - 3);
                return true;
            }
        }
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
    modal_->SetBodyFrameScrolling(false);
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

void GitModal::OpenBranches() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->OpenBranches();
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
