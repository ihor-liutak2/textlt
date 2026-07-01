#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

#include "modals/modal_interface.hpp"
#include "modals/modal_window.hpp"
#include "remote/remote_config_store.hpp"
#include "theme.hpp"

namespace textlt {

class RemoteConnectionsModalContent : public IModalContent {
public:
    using CloseCallback = std::function<void()>;

    RemoteConnectionsModalContent(
        const Theme* theme,
        RemoteConfigStore* config_store,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Remote Connections"; }
    ModalSizePreference GetModalSizePreference() const override { return {112, 31}; }
    std::string GetFooterText() const override { return status_; }

    void Open();
    void Close();
    void Reload();

private:
    ftxui::Component MakeTextButton(std::string label, std::function<void()> on_click);
    ftxui::Element RenderConnectionList();
    ftxui::Element RenderForm();
    ftxui::Element RenderTypeButtons();
    ftxui::Element RenderOutput();
    bool HandleListEvent(ftxui::Event event);
    int EntryIndexAtMouse(const ftxui::Mouse& mouse) const;

    RemoteConnectionType CurrentType() const;
    void SelectType(RemoteConnectionType type);
    void ApplyTypeDefaults(RemoteConnectionType type);
    void LoadSelectedIntoForm();
    void SaveFormToSelected();
    void AddConnection();
    void DeleteSelected();
    void TestSelected();
    void SelectConnection(int index);
    RemoteConnectionConfig FormConfig() const;
    void SetStatus(std::string status, bool is_error = false);

    const Theme* theme_ = nullptr;
    RemoteConfigStore* config_store_ = nullptr;
    CloseCallback on_close_;

    std::vector<RemoteConnectionConfig> connections_;
    int selected_connection_ = 0;
    std::vector<ftxui::Box> connection_boxes_;

    std::string id_value_;
    std::string name_value_;
    std::string type_value_ = "sftp";
    std::string host_value_;
    std::string port_value_ = "22";
    std::string user_value_;
    std::string remote_root_value_ = "/";
    std::string identity_file_value_;
    std::string ssh_config_host_value_;
    std::string account_label_value_;
    std::string client_id_value_;
    std::string client_secret_value_;
    std::string tenant_id_value_;
    std::string token_file_value_;
    std::string root_folder_id_value_;
    std::string site_id_value_;
    std::string drive_id_value_;
    std::string app_key_value_;
    std::string app_secret_value_;

    int name_cursor_ = 0;
    int host_cursor_ = 0;
    int port_cursor_ = 0;
    int user_cursor_ = 0;
    int remote_root_cursor_ = 0;
    int identity_file_cursor_ = 0;
    int ssh_config_host_cursor_ = 0;
    int account_label_cursor_ = 0;
    int client_id_cursor_ = 0;
    int client_secret_cursor_ = 0;
    int tenant_id_cursor_ = 0;
    int token_file_cursor_ = 0;
    int root_folder_id_cursor_ = 0;
    int site_id_cursor_ = 0;
    int drive_id_cursor_ = 0;
    int app_key_cursor_ = 0;
    int app_secret_cursor_ = 0;

    std::string status_ = "Ready.";
    bool status_is_error_ = false;
    std::string output_;

    ftxui::Component list_component_;
    ftxui::Component name_input_;
    ftxui::Component host_input_;
    ftxui::Component port_input_;
    ftxui::Component user_input_;
    ftxui::Component remote_root_input_;
    ftxui::Component identity_file_input_;
    ftxui::Component ssh_config_host_input_;
    ftxui::Component account_label_input_;
    ftxui::Component client_id_input_;
    ftxui::Component client_secret_input_;
    ftxui::Component tenant_id_input_;
    ftxui::Component token_file_input_;
    ftxui::Component root_folder_id_input_;
    ftxui::Component site_id_input_;
    ftxui::Component drive_id_input_;
    ftxui::Component app_key_input_;
    ftxui::Component app_secret_input_;
    ftxui::Component add_button_;
    ftxui::Component delete_button_;
    ftxui::Component save_button_;
    ftxui::Component test_button_;
    ftxui::Component sftp_type_button_;
    ftxui::Component google_type_button_;
    ftxui::Component microsoft_type_button_;
    ftxui::Component dropbox_type_button_;
    ftxui::Component reload_button_;
    ftxui::Component close_button_;
    ftxui::Component container_;
};

class RemoteConnectionsModal {
public:
    RemoteConnectionsModal(const Theme* theme, RemoteConfigStore* config_store);

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
