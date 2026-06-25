#include "modals/modal_git.hpp"

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
        } else if (selected_tab_ == static_cast<int>(Tab::Commit) && commit_file_menu_) {
            commit_file_menu_->TakeFocus();
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
