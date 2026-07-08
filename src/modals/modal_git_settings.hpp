#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "git_manager.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class GitSettingsModalContent : public IModalContent {
public:
    using WriteClipboardCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void()>;

    GitSettingsModalContent(
        const Theme* theme,
        GitManager* git_manager,
        WriteClipboardCallback write_clipboard,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Git Settings"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {112, 32}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();
    bool HandleEvent(ftxui::Event event);

private:
    enum class Tab {
        Remotes = 0,
        Identity = 1,
        Config = 2,
    };

    ftxui::Component MakeTextButton(std::string label, std::function<void()> on_click);
    ftxui::Component MakeTabButton(std::string label, int tab_index);

    void RefreshAll();
    void RefreshRemotes();
    void RefreshIdentity();
    void RefreshConfig();
    void RebuildRemoteLabels();
    void RebuildConfigLabels();
    void ApplySelectedRemoteToInputs();

    void AddRemote();
    void UpdateRemoteUrl();
    void RenameRemote();
    void RemoveRemote();
    void TestRemote();

    void SaveLocalIdentity();
    void SaveGlobalIdentity();
    void ClearLocalIdentity();

    void CopyConfig();
    void SetConfigScope(bool global_scope);

    void RequestConfirm(
        const std::string& title,
        const std::string& message,
        const std::string& command_preview,
        std::function<void()> on_confirm,
        const std::string& required_text = "");
    void ConfirmPendingAction();
    void CancelPendingAction();
    bool HandleConfirmEvent(ftxui::Event event);
    ftxui::Element RenderConfirmOverlay();

    void RunAndRefresh(const std::string& action, const GitManager::CommandResult& result);
    std::string SelectedRemoteName() const;
    std::string TrimForDisplay(const std::string& text, size_t max_size) const;
    std::vector<std::string> SplitLines(const std::string& text) const;
    ftxui::Element RenderTextLines(
        const std::vector<std::string>& lines,
        const std::string& empty_text,
        int scroll_offset = 0) const;

    ftxui::Element RenderHeaderRow();
    ftxui::Element RenderRemotesTab();
    ftxui::Element RenderIdentityTab();
    ftxui::Element RenderConfigTab();

    const Theme* theme_ = nullptr;
    GitManager* git_manager_ = nullptr;
    WriteClipboardCallback write_clipboard_;
    CloseCallback on_close_;

    int selected_tab_ = 0;
    std::string status_ = "Ready";

    std::vector<GitManager::RemoteEntry> remotes_;
    std::vector<std::string> remote_labels_;
    int selected_remote_ = 0;
    std::string remote_name_input_;
    std::string remote_url_input_;
    std::string remote_output_;
    std::vector<std::string> remote_output_lines_;
    int remote_output_scroll_offset_ = 0;

    GitManager::GitIdentity identity_;
    std::string local_name_input_;
    std::string local_email_input_;
    std::string global_name_input_;
    std::string global_email_input_;

    bool config_global_scope_ = false;
    std::string config_filter_;
    std::vector<std::string> config_lines_;
    std::vector<std::string> filtered_config_lines_;
    std::vector<std::string> config_labels_;
    int selected_config_line_ = 0;

    bool confirm_active_ = false;
    int confirm_layer_index_ = 0;
    std::string confirm_title_;
    std::string confirm_message_;
    std::string confirm_command_preview_;
    std::string confirm_required_text_;
    std::string confirm_typed_text_;
    std::function<void()> confirm_action_;

    ftxui::Component remotes_tab_button_;
    ftxui::Component identity_tab_button_;
    ftxui::Component config_tab_button_;
    ftxui::Component tab_buttons_;

    ftxui::Component remote_menu_;
    ftxui::Component remote_name_input_component_;
    ftxui::Component remote_url_input_component_;
    ftxui::Component refresh_remotes_button_;
    ftxui::Component add_remote_button_;
    ftxui::Component update_remote_button_;
    ftxui::Component rename_remote_button_;
    ftxui::Component remove_remote_button_;
    ftxui::Component test_remote_button_;

    ftxui::Component local_name_input_component_;
    ftxui::Component local_email_input_component_;
    ftxui::Component global_name_input_component_;
    ftxui::Component global_email_input_component_;
    ftxui::Component refresh_identity_button_;
    ftxui::Component save_local_identity_button_;
    ftxui::Component save_global_identity_button_;
    ftxui::Component clear_local_identity_button_;

    ftxui::Component local_config_button_;
    ftxui::Component global_config_button_;
    ftxui::Component config_filter_input_component_;
    ftxui::Component config_menu_;
    ftxui::Component refresh_config_button_;
    ftxui::Component copy_config_button_;

    ftxui::Component confirm_input_component_;
    ftxui::Component confirm_confirm_button_;
    ftxui::Component confirm_cancel_button_;
    ftxui::Component confirm_container_;

    ftxui::Component remotes_tab_container_;
    ftxui::Component identity_tab_container_;
    ftxui::Component config_tab_container_;
    ftxui::Component tab_body_container_;
    ftxui::Component container_;
};

class GitSettingsModal {
public:
    using WriteClipboardCallback = GitSettingsModalContent::WriteClipboardCallback;

    GitSettingsModal() = default;

    void Configure(
        const Theme* theme,
        GitManager* git_manager,
        WriteClipboardCallback write_clipboard,
        GitSettingsModalContent::CloseCallback on_close);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<GitSettingsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
