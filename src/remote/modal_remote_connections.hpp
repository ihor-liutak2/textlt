#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

#include "modals/modal_interface.hpp"
#include "modals/modal_window.hpp"
#include "remote/remote_config_store.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt {

class RemoteConnectionsModalContent : public IModalContent {
public:
    using CloseCallback = std::function<void()>;
    using WriteClipboardCallback = std::function<void(const std::string&)>;

    RemoteConnectionsModalContent(
        const Theme* theme,
        RemoteConfigStore* config_store,
        WriteClipboardCallback write_clipboard,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Remote Connections"; }
    ModalSizePreference GetModalSizePreference() const override { return {112, 31}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override { return 1; }
    ftxui::Element RenderCustomFooter() override;

    void Open();
    void Close();
    void Reload();
    bool HandleEvent(ftxui::Event event);

private:
    enum class MainTab {
        Connections,
        Sftp,
        Ftps,
        Dropbox,
    };

    ftxui::Component MakeTextButton(
        std::string label,
        std::function<void()> on_click,
        ButtonRole role = ButtonRole::Default,
        ButtonVariant variant = ButtonVariant::AccentEdges,
        std::string icon = {},
        ButtonSize size = ButtonSize::Normal);
    ftxui::Component MakeTabButton(std::string label, MainTab tab);
    ftxui::Element RenderCurrentTab();
    ftxui::Element RenderConnectionsTab();
    ftxui::Element RenderSftpTab();
    ftxui::Element RenderFtpsTab();
    ftxui::Element RenderDropboxTab();
    ftxui::Element RenderConnectionList();
    ftxui::Element RenderConnectionDetails();
    ftxui::Element RenderHelpOverlay();
    ftxui::Element RenderTabButtons();
    ftxui::Element RenderOutput(int max_lines = 4);
    ftxui::Element RenderActionMessage();
    ftxui::Element RenderFieldGrid(const std::vector<std::pair<std::string, ftxui::Component>>& fields);
    bool HandleListEvent(ftxui::Event event);
    int EntryIndexAtMouse(const ftxui::Mouse& mouse) const;

    RemoteConnectionType CurrentType() const;
    void SelectTab(MainTab tab);
    void SelectType(RemoteConnectionType type);
    void ApplyTypeDefaults(RemoteConnectionType type);
    void ResetFormForType(RemoteConnectionType type);
    void AddConnectionOfType(RemoteConnectionType type);
    void LoadSelectedIntoForm();
    void SaveFormToSelected();
    void EditSelected();
    void DeleteSelected();
    void TestSelected();
    void SetSelectedActive();
    void SetSelectedForNotesSync();
    void PrepareTokenFile();
    void SaveCloudAccessToken();
    void OpenHelp();
    void CloseHelp();
    void CopyHelpUrl();
    void CopyActionMessage();
    void RebuildFooterActions();
    void ReloadSshConfigHosts();
    void SyncSshConfigHostSelection();
    void SelectSshConfigHostValue(const std::string& host);
    bool HandleHelpEvent(ftxui::Event event);
    std::vector<ftxui::Component> GetVisibleInputs();
    int FindFocusedInputIndex(const std::vector<ftxui::Component>& inputs);
    void SelectConnection(int index);
    RemoteConnectionConfig FormConfig() const;
    void SetStatus(std::string status, bool is_error = false);
    std::string ActionMessageText() const;
    std::string SuggestedTokenFile(RemoteConnectionType type) const;
    RemoteConnectionType TypeForTab(MainTab tab) const;
    MainTab TabForType(RemoteConnectionType type) const;
    std::string ActiveConnectionLabel() const;
    bool IsCloudEditorActive() const;
    RemoteConnectionConfig SelectedConnectionConfig() const;
    RemoteConnectionConfig ActionConfig() const;

    const Theme* theme_ = nullptr;
    RemoteConfigStore* config_store_ = nullptr;
    WriteClipboardCallback write_clipboard_;
    CloseCallback on_close_;

    std::vector<RemoteConnectionConfig> connections_;
    int selected_connection_ = 0;
    std::vector<ftxui::Box> connection_boxes_;
    MainTab selected_tab_ = MainTab::Connections;

    std::string id_value_;
    std::string name_value_;
    std::string type_value_ = "sftp";
    std::string host_value_;
    std::string port_value_ = "22";
    std::string user_value_;
    std::string password_value_;
    std::string remote_root_value_ = "/";
    std::string auth_mode_value_ = "auto";
    std::string identity_file_value_;
    std::string key_passphrase_value_;
    std::string known_hosts_file_value_;
    std::string ssh_config_host_value_;
    std::string token_file_value_;
    std::string app_key_value_;
    std::string app_secret_value_;
    std::string access_token_value_;
    std::string refresh_token_value_;
    std::string ftps_tls_mode_value_ = "explicit";
    bool ftps_passive_value_ = true;

    int name_cursor_ = 0;
    int host_cursor_ = 0;
    int port_cursor_ = 0;
    int user_cursor_ = 0;
    int password_cursor_ = 0;
    int remote_root_cursor_ = 0;
    int auth_mode_cursor_ = 0;
    int identity_file_cursor_ = 0;
    int key_passphrase_cursor_ = 0;
    int known_hosts_file_cursor_ = 0;
    int app_key_cursor_ = 0;
    int app_secret_cursor_ = 0;
    int access_token_cursor_ = 0;
    int refresh_token_cursor_ = 0;
    int ftps_tls_mode_cursor_ = 0;

    std::string status_ = "Ready.";
    bool status_is_error_ = false;
    std::string output_;
    bool help_active_ = false;

    ftxui::Component list_component_;
    ftxui::Component name_input_;
    ftxui::Component host_input_;
    ftxui::Component port_input_;
    ftxui::Component user_input_;
    ftxui::Component password_input_;
    ftxui::Component remote_root_input_;
    ftxui::Component auth_mode_input_;
    ftxui::Component identity_file_input_;
    ftxui::Component key_passphrase_input_;
    ftxui::Component known_hosts_file_input_;
    std::vector<std::string> ssh_config_hosts_;
    int selected_ssh_config_host_ = 0;
    ftxui::Component ssh_config_host_menu_;
    ftxui::Component app_key_input_;
    ftxui::Component app_secret_input_;
    ftxui::Component access_token_input_;
    ftxui::Component refresh_token_input_;
    ftxui::Component ftps_tls_mode_input_;
    ftxui::Component ftps_passive_checkbox_;
    ftxui::Component delete_button_;
    ftxui::Component new_button_;
    ftxui::Component save_button_;
    ftxui::Component test_button_;
    ftxui::Component set_active_button_;
    ftxui::Component notes_sync_button_;
    ftxui::Component save_token_button_;
    ftxui::Component edit_button_;
    ftxui::Component connections_tab_button_;
    ftxui::Component sftp_tab_button_;
    ftxui::Component ftps_tab_button_;
    ftxui::Component dropbox_tab_button_;
    ftxui::Component reload_button_;
    ftxui::Component copy_message_button_;
    ftxui::Component close_button_;
    ftxui::Component help_button_;
    ftxui::Component help_close_button_;
    ftxui::Component copy_url_button_;
    ftxui::Component help_container_;
    int help_layer_index_ = 0;
    ftxui::Component footer_actions_container_;
    ftxui::Component container_;
};

class RemoteConnectionsModal {
public:
    using WriteClipboardCallback = RemoteConnectionsModalContent::WriteClipboardCallback;

    RemoteConnectionsModal(
        const Theme* theme,
        RemoteConfigStore* config_store,
        WriteClipboardCallback write_clipboard);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<RemoteConnectionsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
