#include "modals/modal_git_settings.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "ui_button.hpp"

namespace textlt {
namespace {

ButtonSpec GitSettingsButtonSpec(std::string label) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.variant = ButtonVariant::Minimal;

    const std::string& caption = spec.caption;
    if (caption == "Add" || caption == "Update URL" || caption == "Save local" ||
        caption == "Save global" || caption == "Confirm" || caption == "Refresh" ||
        caption == "Test") {
        spec.role = ButtonRole::Primary;
        return spec;
    }
    if (caption == "Cancel" || caption == "Close") {
        spec.role = ButtonRole::Cancel;
        return spec;
    }
    if (caption == "Remove") {
        spec.role = ButtonRole::Danger;
        return spec;
    }
    if (caption == "Clear local") {
        spec.role = ButtonRole::Warning;
        return spec;
    }
    if (caption == "Copy") {
        spec.role = ButtonRole::Utility;
        return spec;
    }
    if (caption == "Local" || caption == "Global") {
        spec.role = ButtonRole::Toggle;
        spec.size = ButtonSize::Compact;
        return spec;
    }
    if (caption == "Rename") {
        spec.role = ButtonRole::Warning;
        return spec;
    }

    spec.role = ButtonRole::Secondary;
    return spec;
}

std::string TrimCopy(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
            value.back() == '\t')) {
        value.pop_back();
    }
    size_t start = 0;
    while (start < value.size() &&
           (value[start] == '\n' || value[start] == '\r' || value[start] == ' ' ||
            value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    std::string haystack_lower = haystack;
    std::string needle_lower = needle;
    std::transform(haystack_lower.begin(), haystack_lower.end(), haystack_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::transform(needle_lower.begin(), needle_lower.end(), needle_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return haystack_lower.find(needle_lower) != std::string::npos;
}

} // namespace

GitSettingsModalContent::GitSettingsModalContent(
    const Theme* theme,
    GitManager* git_manager,
    WriteClipboardCallback write_clipboard,
    CloseCallback on_close)
    : theme_(theme),
      git_manager_(git_manager),
      write_clipboard_(std::move(write_clipboard)),
      on_close_(std::move(on_close)) {
    remotes_tab_button_ = MakeTabButton("Remotes", static_cast<int>(Tab::Remotes));
    identity_tab_button_ = MakeTabButton("Identity", static_cast<int>(Tab::Identity));
    config_tab_button_ = MakeTabButton("Config", static_cast<int>(Tab::Config));

    tab_buttons_ = ftxui::Container::Horizontal({
        remotes_tab_button_,
        identity_tab_button_,
        config_tab_button_,
    });

    ftxui::MenuOption remote_menu_option = ftxui::MenuOption::Vertical();
    remote_menu_option.on_change = [this] { ApplySelectedRemoteToInputs(); };
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
    remote_menu_ = ftxui::Menu(&remote_labels_, &selected_remote_, remote_menu_option);

    ftxui::InputOption remote_name_option;
    remote_name_option.multiline = false;
    remote_name_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    remote_name_input_component_ = ftxui::Input(&remote_name_input_, "remote name", remote_name_option);

    ftxui::InputOption remote_url_option;
    remote_url_option.multiline = false;
    remote_url_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    remote_url_input_component_ = ftxui::Input(&remote_url_input_, "remote url", remote_url_option);

    refresh_remotes_button_ = MakeTextButton("Refresh", [this] { RefreshRemotes(); });
    add_remote_button_ = MakeTextButton("Add", [this] { AddRemote(); });
    update_remote_button_ = MakeTextButton("Update URL", [this] { UpdateRemoteUrl(); });
    rename_remote_button_ = MakeTextButton("Rename", [this] { RenameRemote(); });
    remove_remote_button_ = MakeTextButton("Remove", [this] { RemoveRemote(); });
    test_remote_button_ = MakeTextButton("Test", [this] { TestRemote(); });

    ftxui::InputOption identity_option;
    identity_option.multiline = false;
    identity_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    local_name_input_component_ = ftxui::Input(&local_name_input_, "local user.name", identity_option);
    local_email_input_component_ = ftxui::Input(&local_email_input_, "local user.email", identity_option);
    global_name_input_component_ = ftxui::Input(&global_name_input_, "global user.name", identity_option);
    global_email_input_component_ = ftxui::Input(&global_email_input_, "global user.email", identity_option);

    refresh_identity_button_ = MakeTextButton("Refresh", [this] { RefreshIdentity(); });
    save_local_identity_button_ = MakeTextButton("Save local", [this] { SaveLocalIdentity(); });
    save_global_identity_button_ = MakeTextButton("Save global", [this] { SaveGlobalIdentity(); });
    clear_local_identity_button_ = MakeTextButton("Clear local", [this] { ClearLocalIdentity(); });

    local_config_button_ = MakeTextButton("Local", [this] { SetConfigScope(false); });
    global_config_button_ = MakeTextButton("Global", [this] { SetConfigScope(true); });

    ftxui::InputOption config_filter_option;
    config_filter_option.multiline = false;
    config_filter_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    auto config_filter_input = ftxui::Input(&config_filter_, "filter config", config_filter_option);
    config_filter_input_component_ = ftxui::CatchEvent(
        config_filter_input,
        [this, config_filter_input](ftxui::Event event) {
            const bool handled = config_filter_input->OnEvent(event);
            if (handled) {
                RebuildConfigLabels();
            }
            return handled;
        });

    ftxui::MenuOption config_menu_option = ftxui::MenuOption::Vertical();
    config_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
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
    config_menu_ = ftxui::Menu(&config_labels_, &selected_config_line_, config_menu_option);

    refresh_config_button_ = MakeTextButton("Refresh", [this] { RefreshConfig(); });
    copy_config_button_ = MakeTextButton("Copy", [this] { CopyConfig(); });
    footer_close_button_ = MakeTextButton("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    });

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
            return false;
        });

    remotes_tab_container_ = ftxui::Container::Vertical({
        remote_menu_,
        remote_name_input_component_,
        remote_url_input_component_,
    });

    identity_tab_container_ = ftxui::Container::Vertical({
        local_name_input_component_,
        local_email_input_component_,
        save_local_identity_button_,
        global_name_input_component_,
        global_email_input_component_,
        save_global_identity_button_,
    });

    config_tab_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            local_config_button_,
            global_config_button_,
        }),
        config_filter_input_component_,
        config_menu_,
    });

    tab_body_container_ = ftxui::Container::Tab({
        remotes_tab_container_,
        identity_tab_container_,
        config_tab_container_,
    }, &selected_tab_);

    auto primary_container = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
        refresh_remotes_button_,
        add_remote_button_,
        update_remote_button_,
        rename_remote_button_,
        remove_remote_button_,
        test_remote_button_,
        refresh_identity_button_,
        clear_local_identity_button_,
        refresh_config_button_,
        copy_config_button_,
        footer_close_button_,
    });
    primary_container = ftxui::CatchEvent(
        primary_container,
        [this](ftxui::Event event) { return HandleEvent(std::move(event)); });
    container_ = ftxui::Container::Tab(
        {primary_container, confirm_container_}, &confirm_layer_index_);
}

ftxui::Component GitSettingsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = GitSettingsButtonSpec(std::move(label));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

ftxui::Component GitSettingsModalContent::MakeTabButton(std::string label, int tab_index) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = "  " + label + "  ";
    option.on_click = [this, tab_index] {
        selected_tab_ = tab_index;
        if (selected_tab_ == static_cast<int>(Tab::Remotes) && remote_menu_) {
            remote_menu_->TakeFocus();
        } else if (selected_tab_ == static_cast<int>(Tab::Identity) && local_name_input_component_) {
            local_name_input_component_->TakeFocus();
        } else if (selected_tab_ == static_cast<int>(Tab::Config) && config_filter_input_component_) {
            config_filter_input_component_->TakeFocus();
        }
    };
    option.transform = [this, tab_index, label = std::move(label)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalTabButton(
            theme,
            label,
            selected_tab_ == tab_index,
            state.focused || state.active);
    };
    return ftxui::Button(option);
}

void GitSettingsModalContent::Open() {
    confirm_active_ = false;
    confirm_layer_index_ = 0;
    selected_tab_ = static_cast<int>(Tab::Remotes);
    status_ = "Ready";
    remote_output_.clear();
    remote_output_lines_.clear();
    remote_output_scroll_offset_ = 0;
    RefreshAll();
    if (remote_menu_) {
        remote_menu_->TakeFocus();
    }
}

void GitSettingsModalContent::Close() {
    confirm_active_ = false;
    confirm_layer_index_ = 0;
}

ftxui::Element GitSettingsModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

ftxui::Element GitSettingsModalContent::RenderHeaderRow() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        remotes_tab_button_->Render(),
        text(" "),
        identity_tab_button_->Render(),
        text(" "),
        config_tab_button_->Render(),
        filler(),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

ftxui::Element GitSettingsModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Element body;
    if (selected_tab_ == static_cast<int>(Tab::Remotes)) {
        body = RenderRemotesTab();
    } else if (selected_tab_ == static_cast<int>(Tab::Identity)) {
        body = RenderIdentityTab();
    } else {
        body = RenderConfigTab();
    }

    body = ftxui::vbox({
        RenderHeaderRow(),
        ftxui::separator() | ftxui::color(theme.modal_border),
        body | ftxui::flex,
    }) |
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

bool GitSettingsModalContent::HandleEvent(ftxui::Event event) {
    if (confirm_active_) {
        return HandleConfirmEvent(event);
    }

    if (event == ftxui::Event::Escape) {
        if (on_close_) {
            on_close_();
        }
        return true;
    }

    if (selected_tab_ == static_cast<int>(Tab::Remotes)) {
        const bool wheel_down = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelDown;
        const bool wheel_up = event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelUp;
        if (event == ftxui::Event::PageDown || wheel_down) {
            remote_output_scroll_offset_ += 8;
            return true;
        }
        if (event == ftxui::Event::PageUp || wheel_up) {
            remote_output_scroll_offset_ = std::max(0, remote_output_scroll_offset_ - 8);
            return true;
        }
    }

    return false;
}

void GitSettingsModalContent::RefreshAll() {
    RefreshRemotes();
    RefreshIdentity();
    RefreshConfig();
}

void GitSettingsModalContent::RefreshRemotes() {
    if (!git_manager_) {
        return;
    }
    remotes_ = git_manager_->GetRemotes();
    RebuildRemoteLabels();
    ApplySelectedRemoteToInputs();
    status_ = "Remotes refreshed.";
}

void GitSettingsModalContent::RefreshIdentity() {
    if (!git_manager_) {
        return;
    }
    identity_ = git_manager_->GetIdentity();
    local_name_input_ = identity_.local_name;
    local_email_input_ = identity_.local_email;
    global_name_input_ = identity_.global_name;
    global_email_input_ = identity_.global_email;
    status_ = "Identity refreshed.";
}

void GitSettingsModalContent::RefreshConfig() {
    if (!git_manager_) {
        return;
    }
    config_lines_ = git_manager_->GetConfigList(config_global_scope_);
    RebuildConfigLabels();
    status_ = config_global_scope_ ? "Global config refreshed." : "Local config refreshed.";
}

void GitSettingsModalContent::RebuildRemoteLabels() {
    remote_labels_.clear();
    remote_labels_.reserve(remotes_.size());
    for (const GitManager::RemoteEntry& remote : remotes_) {
        remote_labels_.push_back(
            TrimForDisplay(remote.name, 18) + "  " + TrimForDisplay(remote.fetch_url, 76));
    }
    if (selected_remote_ >= static_cast<int>(remote_labels_.size())) {
        selected_remote_ = static_cast<int>(remote_labels_.size()) - 1;
    }
    if (selected_remote_ < 0) {
        selected_remote_ = 0;
    }
}

void GitSettingsModalContent::RebuildConfigLabels() {
    filtered_config_lines_.clear();
    config_labels_.clear();
    for (const std::string& line : config_lines_) {
        if (!ContainsCaseInsensitive(line, config_filter_)) {
            continue;
        }
        filtered_config_lines_.push_back(line);
        config_labels_.push_back(TrimForDisplay(line, 96));
    }
    if (selected_config_line_ >= static_cast<int>(config_labels_.size())) {
        selected_config_line_ = static_cast<int>(config_labels_.size()) - 1;
    }
    if (selected_config_line_ < 0) {
        selected_config_line_ = 0;
    }
}

void GitSettingsModalContent::ApplySelectedRemoteToInputs() {
    if (selected_remote_ < 0 || selected_remote_ >= static_cast<int>(remotes_.size())) {
        remote_name_input_.clear();
        remote_url_input_.clear();
        return;
    }
    const GitManager::RemoteEntry& remote = remotes_[selected_remote_];
    remote_name_input_ = remote.name;
    remote_url_input_ = remote.fetch_url;
}

void GitSettingsModalContent::AddRemote() {
    if (!git_manager_) {
        return;
    }
    const std::string name = TrimCopy(remote_name_input_);
    const std::string url = TrimCopy(remote_url_input_);
    if (name.empty() || url.empty()) {
        status_ = "Remote name and URL are required.";
        return;
    }
    RunAndRefresh("Add remote", git_manager_->AddRemote(name, url));
}

void GitSettingsModalContent::UpdateRemoteUrl() {
    if (!git_manager_) {
        return;
    }
    const std::string selected_name = SelectedRemoteName();
    const std::string url = TrimCopy(remote_url_input_);
    if (selected_name.empty() || url.empty()) {
        status_ = "Select a remote and enter URL.";
        return;
    }
    RequestConfirm(
        "Confirm remote URL update",
        "Update the selected remote URL.",
        "git remote set-url " + selected_name + " " + url,
        [this, selected_name, url] {
            RunAndRefresh("Update remote URL", git_manager_->SetRemoteUrl(selected_name, url));
        });
}

void GitSettingsModalContent::RenameRemote() {
    if (!git_manager_) {
        return;
    }
    const std::string old_name = SelectedRemoteName();
    const std::string new_name = TrimCopy(remote_name_input_);
    if (old_name.empty() || new_name.empty()) {
        status_ = "Select a remote and enter a new name.";
        return;
    }
    if (old_name == new_name) {
        status_ = "Remote name was not changed.";
        return;
    }
    RequestConfirm(
        "Confirm remote rename",
        "Rename the selected remote.",
        "git remote rename " + old_name + " " + new_name,
        [this, old_name, new_name] {
            RunAndRefresh("Rename remote", git_manager_->RenameRemote(old_name, new_name));
        });
}

void GitSettingsModalContent::RemoveRemote() {
    if (!git_manager_) {
        return;
    }
    const std::string name = SelectedRemoteName();
    if (name.empty()) {
        status_ = "No remote selected.";
        return;
    }
    RequestConfirm(
        "Confirm remote remove",
        "Remove selected remote from this repository.",
        "git remote remove " + name,
        [this, name] {
            RunAndRefresh("Remove remote", git_manager_->RemoveRemote(name));
        },
        "REMOVE");
}

void GitSettingsModalContent::TestRemote() {
    if (!git_manager_) {
        return;
    }
    const std::string name = SelectedRemoteName().empty()
        ? TrimCopy(remote_name_input_)
        : SelectedRemoteName();
    if (name.empty()) {
        status_ = "No remote selected.";
        return;
    }
    GitManager::CommandResult result = git_manager_->TestRemote(name);
    remote_output_ = result.output;
    remote_output_scroll_offset_ = 0;
    remote_output_lines_ = SplitLines(remote_output_);
    status_ = result.success() ? "Remote connection OK." : "Remote test failed.";
}

void GitSettingsModalContent::SaveLocalIdentity() {
    if (!git_manager_) {
        return;
    }
    if (TrimCopy(local_name_input_).empty() || TrimCopy(local_email_input_).empty()) {
        status_ = "Local name and email are required.";
        return;
    }
    RunAndRefresh(
        "Save local identity",
        git_manager_->SaveLocalIdentity(local_name_input_, local_email_input_));
}

void GitSettingsModalContent::SaveGlobalIdentity() {
    if (!git_manager_) {
        return;
    }
    if (TrimCopy(global_name_input_).empty() || TrimCopy(global_email_input_).empty()) {
        status_ = "Global name and email are required.";
        return;
    }
    RequestConfirm(
        "Confirm global identity update",
        "Update global Git user.name and user.email.",
        "git config --global user.name/user.email",
        [this] {
            RunAndRefresh(
                "Save global identity",
                git_manager_->SaveGlobalIdentity(global_name_input_, global_email_input_));
        });
}

void GitSettingsModalContent::ClearLocalIdentity() {
    if (!git_manager_) {
        return;
    }
    RequestConfirm(
        "Confirm clear local identity",
        "Remove local user.name and user.email from this repository.",
        "git config --unset --local user.name/user.email",
        [this] { RunAndRefresh("Clear local identity", git_manager_->ClearLocalIdentity()); });
}

void GitSettingsModalContent::CopyConfig() {
    if (!write_clipboard_) {
        return;
    }
    std::string text;
    for (const std::string& line : filtered_config_lines_) {
        if (!text.empty()) {
            text += '\n';
        }
        text += line;
    }
    write_clipboard_(text);
    status_ = "Copied config lines.";
}

void GitSettingsModalContent::SetConfigScope(bool global_scope) {
    config_global_scope_ = global_scope;
    RefreshConfig();
}

void GitSettingsModalContent::RequestConfirm(
    const std::string& title,
    const std::string& message,
    const std::string& command_preview,
    std::function<void()> on_confirm,
    const std::string& required_text) {
    confirm_title_ = title;
    confirm_message_ = message;
    confirm_command_preview_ = command_preview;
    confirm_action_ = std::move(on_confirm);
    confirm_required_text_ = required_text;
    confirm_typed_text_.clear();
    confirm_active_ = true;
    confirm_layer_index_ = 1;
    if (confirm_required_text_.empty()) {
        confirm_confirm_button_->TakeFocus();
    } else {
        confirm_input_component_->TakeFocus();
    }
}

void GitSettingsModalContent::ConfirmPendingAction() {
    if (!confirm_active_) {
        return;
    }
    if (!confirm_required_text_.empty() && confirm_typed_text_ != confirm_required_text_) {
        status_ = "Confirmation text does not match.";
        return;
    }
    auto action = std::move(confirm_action_);
    confirm_active_ = false;
    confirm_layer_index_ = 0;
    confirm_action_ = nullptr;
    if (action) {
        action();
    }
}

void GitSettingsModalContent::CancelPendingAction() {
    confirm_active_ = false;
    confirm_layer_index_ = 0;
    confirm_action_ = nullptr;
    confirm_typed_text_.clear();
    status_ = "Action cancelled.";
}

bool GitSettingsModalContent::HandleConfirmEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        CancelPendingAction();
        return true;
    }
    return false;
}

ftxui::Element GitSettingsModalContent::RenderConfirmOverlay() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ftxui::Elements rows = {
        ftxui::text(confirm_title_) | ftxui::bold,
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::paragraph(confirm_message_),
        ftxui::text("Command:"),
        ftxui::paragraph(confirm_command_preview_) | ftxui::color(theme.modal_accent),
    };
    if (!confirm_required_text_.empty()) {
        rows.push_back(ftxui::text("Type " + confirm_required_text_ + " to confirm:"));
        rows.push_back(confirm_input_component_->Render() |
                       ftxui::borderStyled(ftxui::LIGHT, theme.modal_border));
    }
    rows.push_back(ftxui::hbox({
        confirm_confirm_button_->Render(),
        ftxui::text(" "),
        confirm_cancel_button_->Render(),
    }));
    return ftxui::vbox(std::move(rows)) |
        ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 66) |
        ftxui::borderStyled(ftxui::LIGHT, theme.modal_border) |
        ftxui::bgcolor(theme.modal_background) |
        ftxui::color(theme.modal_foreground);
}

void GitSettingsModalContent::RunAndRefresh(
    const std::string& action,
    const GitManager::CommandResult& result) {
    if (!result.output.empty()) {
        remote_output_ = result.output;
        remote_output_scroll_offset_ = 0;
        remote_output_lines_ = SplitLines(remote_output_);
    }
    status_ = action + (result.success() ? " succeeded." : " failed.");
    RefreshAll();
}

std::string GitSettingsModalContent::SelectedRemoteName() const {
    if (selected_remote_ < 0 || selected_remote_ >= static_cast<int>(remotes_.size())) {
        return "";
    }
    return remotes_[selected_remote_].name;
}

std::string GitSettingsModalContent::TrimForDisplay(const std::string& text, size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }
    if (max_size <= 3) {
        return text.substr(0, max_size);
    }
    return text.substr(0, max_size - 3) + "...";
}

std::vector<std::string> GitSettingsModalContent::SplitLines(const std::string& text) const {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

ftxui::Element GitSettingsModalContent::RenderTextLines(
    const std::vector<std::string>& lines,
    const std::string& empty_text,
    int scroll_offset) const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ftxui::Elements rows;
    if (lines.empty()) {
        rows.push_back(ftxui::text(empty_text) | ftxui::dim);
    } else {
        const int start = std::max(0, scroll_offset);
        const int max_rows = 10;
        const int end = std::min(static_cast<int>(lines.size()), start + max_rows);
        for (int index = start; index < end; ++index) {
            rows.push_back(ftxui::text(TrimForDisplay(lines[index], 100)) |
                           ftxui::color(theme.modal_text_color));
        }
    }
    return ftxui::vbox(std::move(rows));
}

ftxui::Element GitSettingsModalContent::RenderRemotesTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        text("Remotes") | bold,
        hbox({
            vbox({
                remote_menu_->Render() |
                    frame |
                    vscroll_indicator |
                    size(HEIGHT, LESS_THAN, 9) |
                    borderStyled(LIGHT, theme.modal_border),
            }) | flex,
            separator() | color(theme.modal_border),
            vbox({
                text("Name:"),
                remote_name_input_component_->Render() |
                    borderStyled(LIGHT, theme.modal_border),
                text("URL:"),
                remote_url_input_component_->Render() |
                    borderStyled(LIGHT, theme.modal_border),
            }) | size(WIDTH, GREATER_THAN, 45),
        }) | flex,
        separator() | color(theme.modal_border),
        text("Output") | bold,
        RenderTextLines(remote_output_lines_, "Run Test or another remote command to see output.", remote_output_scroll_offset_) |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 10) |
            borderStyled(LIGHT, theme.modal_border),
    }) | color(theme.modal_foreground);
}

ftxui::Element GitSettingsModalContent::RenderIdentityTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        text("Effective user") | bold,
        hbox({text("Name: "), text(identity_.effective_name.empty() ? "<not set>" : identity_.effective_name)}),
        hbox({text("Email: "), text(identity_.effective_email.empty() ? "<not set>" : identity_.effective_email)}),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                text("Local repository config") | bold,
                text("Name:"),
                local_name_input_component_->Render() |
                    borderStyled(LIGHT, theme.modal_border),
                text("Email:"),
                local_email_input_component_->Render() |
                    borderStyled(LIGHT, theme.modal_border),
                save_local_identity_button_->Render(),
            }) |
                borderStyled(LIGHT, theme.modal_border) |
                flex,
            separator() | color(theme.modal_border),
            vbox({
                text("Global config") | bold,
                text("Name:"),
                global_name_input_component_->Render() |
                    borderStyled(LIGHT, theme.modal_border),
                text("Email:"),
                global_email_input_component_->Render() |
                    borderStyled(LIGHT, theme.modal_border),
                save_global_identity_button_->Render(),
            }) |
                borderStyled(LIGHT, theme.modal_border) |
                flex,
        }) | flex,
    }) | color(theme.modal_foreground);
}

ftxui::Element GitSettingsModalContent::RenderConfigTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        hbox({
            text("Scope: "),
            local_config_button_->Render(), text(" "),
            global_config_button_->Render(),
        }),
        text(config_global_scope_ ? "Current scope: Global" : "Current scope: Local") | dim,
        text("Search:"),
        config_filter_input_component_->Render() |
            borderStyled(LIGHT, theme.modal_border),
        separator() | color(theme.modal_border),
        config_menu_->Render() |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 20) |
            borderStyled(LIGHT, theme.modal_border) |
            flex,
    }) | color(theme.modal_foreground);
}

ftxui::Element GitSettingsModalContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements footer_buttons;
    auto append_button = [&footer_buttons](const ftxui::Component& button) {
        if (!button) {
            return;
        }
        if (!footer_buttons.empty()) {
            footer_buttons.push_back(text(" "));
        }
        footer_buttons.push_back(button->Render());
    };

    if (selected_tab_ == static_cast<int>(Tab::Remotes)) {
        append_button(refresh_remotes_button_);
        append_button(add_remote_button_);
        append_button(update_remote_button_);
        append_button(rename_remote_button_);
        append_button(remove_remote_button_);
        append_button(test_remote_button_);
    } else if (selected_tab_ == static_cast<int>(Tab::Identity)) {
        append_button(refresh_identity_button_);
        append_button(clear_local_identity_button_);
    } else {
        append_button(refresh_config_button_);
        append_button(copy_config_button_);
    }

    return vbox({
        hbox({
            text(" " + status_) |
                dim |
                color(theme.modal_text_color),
            filler(),
        }),
        separator() | color(theme.modal_border),
        hbox({
            hbox(std::move(footer_buttons)),
            filler(),
            footer_close_button_ ? footer_close_button_->Render() : text(""),
        }),
    }) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color);
}

void GitSettingsModal::Configure(
    const Theme* theme,
    GitManager* git_manager,
    WriteClipboardCallback write_clipboard,
    GitSettingsModalContent::CloseCallback on_close) {
    theme_ = theme;
    content_ = std::make_shared<GitSettingsModalContent>(
        theme_,
        git_manager,
        std::move(write_clipboard),
        std::move(on_close));
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
}

ftxui::Component GitSettingsModal::View() const {
    return modal_;
}

void GitSettingsModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
    }
}

void GitSettingsModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool GitSettingsModal::IsOpen() const {
    return open_;
}

bool GitSettingsModal::OnEvent(ftxui::Event event) {
    if (!open_) {
        return false;
    }
    if (content_ && content_->HandleEvent(event)) {
        return true;
    }
    return modal_ ? modal_->OnEvent(event) : false;
}

} // namespace textlt
