void RemoteConnectionsModalContent::SelectConnection(int index) {
    if (connections_.empty()) {
        selected_connection_ = 0;
        return;
    }
    selected_connection_ = std::clamp(index, 0, static_cast<int>(connections_.size()) - 1);
}

RemoteConnectionConfig RemoteConnectionsModalContent::FormConfig() const {
    RemoteConnectionConfig config;
    config.id = id_value_;
    config.name = name_value_;
    config.type = CurrentType();
    config.host = host_value_;
    config.port = ParsePort(port_value_);
    config.user = user_value_;
    config.password = password_value_;
    config.remote_root = NormalizeRemoteDirectory(remote_root_value_);
    config.auth_mode = auth_mode_value_.empty() ? "auto" : auth_mode_value_;
    config.identity_file = identity_file_value_;
    config.key_passphrase = key_passphrase_value_;
    config.known_hosts_file = known_hosts_file_value_;
    config.ssh_config_host = ssh_config_host_value_;
    config.client_id = client_id_value_;
    config.client_secret = client_secret_value_;
    config.tenant_id = tenant_id_value_;
    config.token_file = token_file_value_;
    config.root_folder_id = root_folder_id_value_;
    config.site_id = site_id_value_;
    config.drive_id = drive_id_value_;
    config.app_key = app_key_value_;
    config.app_secret = app_secret_value_;
    config.scope = scope_value_;
    return config;
}

std::string RemoteConnectionsModalContent::SuggestedTokenFile(RemoteConnectionType type) const {
    std::string stable_name = name_value_;
    if (stable_name.empty()) {
        stable_name = id_value_;
    }
    return DefaultRemoteTokenPath(type, stable_name).string();
}

RemoteConnectionType RemoteConnectionsModalContent::TypeForTab(MainTab tab) const {
    switch (tab) {
        case MainTab::Sftp:
            return RemoteConnectionType::Sftp;
        case MainTab::GoogleDrive:
            return RemoteConnectionType::GoogleDrive;
        case MainTab::MicrosoftDrive:
            return RemoteConnectionType::MicrosoftDrive;
        case MainTab::Dropbox:
            return RemoteConnectionType::Dropbox;
        case MainTab::Connections:
            break;
    }
    return CurrentType();
}

RemoteConnectionsModalContent::MainTab RemoteConnectionsModalContent::TabForType(RemoteConnectionType type) const {
    switch (type) {
        case RemoteConnectionType::Sftp:
            return MainTab::Sftp;
        case RemoteConnectionType::GoogleDrive:
            return MainTab::GoogleDrive;
        case RemoteConnectionType::MicrosoftDrive:
            return MainTab::MicrosoftDrive;
        case RemoteConnectionType::Dropbox:
            return MainTab::Dropbox;
    }
    return MainTab::Sftp;
}

std::string RemoteConnectionsModalContent::ActiveConnectionLabel() const {
    if (!config_store_) {
        return "none";
    }
    const RemoteConnectionConfig* active = config_store_->FindActiveConnection();
    if (!active) {
        return "none";
    }
    if (!active->name.empty()) {
        return active->name;
    }
    if (!active->ssh_config_host.empty()) {
        return active->ssh_config_host;
    }
    if (!active->host.empty()) {
        return active->host;
    }
    return active->id.empty() ? "unnamed" : active->id;
}

void RemoteConnectionsModalContent::SetStatus(std::string status, bool is_error) {
    status_ = std::move(status);
    status_is_error_ = is_error;
}

ftxui::Element RemoteConnectionsModalContent::RenderHelpOverlay() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const RemoteConnectionType type = selected_tab_ == MainTab::Connections
        ? CurrentType()
        : TypeForTab(selected_tab_);

    auto field_row = [&](const std::string& name, const std::string& description) {
        return hbox({
            text(" " + name) | bold | color(theme.modal_accent) | size(WIDTH, EQUAL, 17),
            text(description) | color(theme.modal_text_color),
        });
    };

    Elements rows;
    rows.push_back(text(" " + TypeDisplayName(type) + " fields ") | bold | color(theme.modal_accent));
    rows.push_back(separator() | color(theme.modal_border));

    switch (type) {
        case RemoteConnectionType::Sftp:
            rows.push_back(field_row("Name", "Local profile name shown in Connections."));
            rows.push_back(field_row("Host", "Server DNS name or IP address for manual SFTP."));
            rows.push_back(field_row("Port", "SSH/SFTP port, usually 22."));
            rows.push_back(field_row("Username", "SSH username for the remote server."));
            rows.push_back(field_row("Password", "SSH password. If used, TextLT needs sshpass for non-interactive sftp."));
            rows.push_back(field_row("Remote root", "Initial remote directory for browsing files."));
            rows.push_back(field_row("Auth mode", "auto, password, private_key, agent, or ssh_config."));
            rows.push_back(field_row("Private key file", "Private SSH key path, for example ~/.ssh/id_ed25519."));
            rows.push_back(field_row("Key passphrase", "Passphrase for an encrypted private key when your SSH setup prompts for it."));
            rows.push_back(field_row("Known hosts file", "Optional known_hosts file path, for example ~/.ssh/known_hosts."));
            rows.push_back(field_row("SSH config host", "Alias from ~/.ssh/config. It can define host, user, port, key, ProxyJump."));
            break;
        case RemoteConnectionType::GoogleDrive:
            rows.push_back(field_row("Name", "Local profile name shown in Connections."));
            rows.push_back(field_row("Root folder ID", "Optional Drive folder id used as the browsing root."));
            rows.push_back(field_row("Client ID", "OAuth client id from Google Cloud Console, not an email address."));
            rows.push_back(field_row("Client secret", "OAuth client secret for that Google client."));
            rows.push_back(field_row("Scope", "OAuth scope. Default is drive.file for narrower Drive access."));
            rows.push_back(field_row("Access token", "Bearer token pasted manually and saved into the token file."));
            rows.push_back(field_row("Refresh token", "Optional OAuth refresh token saved into the token file."));
            break;
        case RemoteConnectionType::MicrosoftDrive:
            rows.push_back(field_row("Name", "Local profile name shown in Connections."));
            rows.push_back(field_row("Tenant ID", "common, consumers, organizations, or a concrete tenant id."));
            rows.push_back(field_row("Client ID", "Application/client id from Microsoft app registration."));
            rows.push_back(field_row("Client secret", "Secret created for the Microsoft app registration."));
            rows.push_back(field_row("Site ID", "Optional SharePoint site id when using SharePoint files."));
            rows.push_back(field_row("Drive ID", "Optional OneDrive/SharePoint drive id. Empty can mean default drive."));
            rows.push_back(field_row("Remote root", "Initial folder path inside the selected drive."));
            rows.push_back(field_row("Scope", "OAuth scope, for example offline_access Files.ReadWrite."));
            rows.push_back(field_row("Access token", "Bearer token pasted manually and saved into the token file."));
            rows.push_back(field_row("Refresh token", "Optional OAuth refresh token saved into the token file."));
            break;
        case RemoteConnectionType::Dropbox:
            rows.push_back(field_row("Name", "Local profile name shown in Connections."));
            rows.push_back(field_row("Remote root", "Initial Dropbox path, usually / or /FolderName."));
            rows.push_back(field_row("App key", "Dropbox app key from the Dropbox developer app page."));
            rows.push_back(field_row("App secret", "Dropbox app secret from the same app page."));
            rows.push_back(field_row("Access token", "OAuth access token generated for the Dropbox app/account and saved into the token file."));
            rows.push_back(field_row("Refresh token", "Optional Dropbox refresh token saved into the token file."));
            break;
    }

    rows.push_back(text(""));
    rows.push_back(hbox({
        filler(),
        help_close_button_ ? help_close_button_->Render() : text(""),
    }));
    return vbox(std::move(rows)) |
        size(WIDTH, LESS_THAN, 86) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color) |
        clear_under;
}

RemoteConnectionsModal::RemoteConnectionsModal(
    const Theme* theme,
    RemoteConfigStore* config_store,
    WriteClipboardCallback write_clipboard)
    : theme_(theme) {
    content_ = std::make_shared<RemoteConnectionsModalContent>(
        theme_,
        config_store,
        std::move(write_clipboard),
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component RemoteConnectionsModal::View() const {
    return modal_;
}

void RemoteConnectionsModal::Open() {
    open_ = true;
    content_->Open();
    if (content_) {
        content_->GetMainComponent()->TakeFocus();
    }
}

void RemoteConnectionsModal::Close() {
    open_ = false;
    content_->Close();
}

bool RemoteConnectionsModal::IsOpen() const {
    return open_;
}

bool RemoteConnectionsModal::OnEvent(ftxui::Event event) {
    if (!open_) {
        return false;
    }
    if (content_ && content_->HandleEvent(event)) {
        return true;
    }
    return modal_ ? modal_->OnEvent(std::move(event)) : false;
}
