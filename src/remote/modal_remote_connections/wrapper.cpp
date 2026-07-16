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
    config.token_file = token_file_value_;
    config.app_key = app_key_value_;
    config.app_secret = app_secret_value_;
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
        case MainTab::Ssh:
            return RemoteConnectionType::Ssh;
        case MainTab::Sftp:
            return RemoteConnectionType::Sftp;
        case MainTab::Dropbox:
            return RemoteConnectionType::Dropbox;
        case MainTab::Connections:
            break;
    }
    return CurrentType();
}

RemoteConnectionsModalContent::MainTab RemoteConnectionsModalContent::TabForType(RemoteConnectionType type) const {
    switch (type) {
        case RemoteConnectionType::Ssh:
            return MainTab::Ssh;
        case RemoteConnectionType::Sftp:
            return MainTab::Sftp;
        case RemoteConnectionType::Dropbox:
            return MainTab::Dropbox;
    }
    return MainTab::Connections;
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

    auto field_row = [&](const std::string& name, const std::string& description) {
        return hbox({
            text(" " + name) | bold | color(theme.modal_accent) | size(WIDTH, EQUAL, 17),
            text(description) | color(theme.modal_text_color),
        });
    };

    Elements rows;
    const std::string title = selected_tab_ == MainTab::Ssh
        ? "SSH fields"
        : selected_tab_ == MainTab::Sftp
            ? "SFTP fields"
            : selected_tab_ == MainTab::Dropbox
                ? "Dropbox fields"
                : "Remote fields";
    rows.push_back(text(" " + title + " ") | bold | color(theme.modal_accent));
    rows.push_back(separator() | color(theme.modal_border));

    switch (selected_tab_) {
        case MainTab::Ssh:
            rows.push_back(field_row("Name", "Local profile name shown in Connections."));
            rows.push_back(field_row("Host", "Server DNS name or IP address for password-based SSH."));
            rows.push_back(field_row("Port", "SSH port, usually 22."));
            rows.push_back(field_row("Username", "SSH username for the remote server."));
            rows.push_back(field_row("Password", "SSH password. TextLT uses sshpass for non-interactive login."));
            rows.push_back(field_row("Remote root", "Initial remote directory for browsing files."));
            break;
        case MainTab::Sftp:
            rows.push_back(field_row("Name", "Local profile name shown in Connections."));
            rows.push_back(field_row("Host", "Server DNS name or IP address for key/file-based access."));
            rows.push_back(field_row("Port", "SSH/SFTP port, usually 22."));
            rows.push_back(field_row("Username", "SSH username for the remote server."));
            rows.push_back(field_row("Remote root", "Initial remote directory for browsing files."));
            rows.push_back(field_row("Auth mode", "auto, private_key, agent, or ssh_config."));
            rows.push_back(field_row("Private key file", "Private SSH key path, for example ~/.ssh/id_ed25519."));
            rows.push_back(field_row("Key passphrase", "Passphrase for an encrypted private key."));
            rows.push_back(field_row("Known hosts file", "Optional known_hosts file path, for example ~/.ssh/known_hosts."));
            rows.push_back(field_row("SSH config host", "Alias from ~/.ssh/config. It can define host, user, port, key, ProxyJump."));
            break;
        case MainTab::Dropbox:
            rows.push_back(field_row("Name", "Local profile name shown in Connections."));
            rows.push_back(field_row("Remote root", "Initial Dropbox path, usually / or /FolderName."));
            rows.push_back(field_row("App key", "Dropbox app key from the Dropbox developer app page."));
            rows.push_back(field_row("App secret", "Dropbox app secret from the same app page."));
            rows.push_back(field_row("Access token", "OAuth access token generated for the Dropbox app/account and saved into the token file."));
            rows.push_back(field_row("Refresh token", "Optional Dropbox refresh token saved into the token file."));
            break;
        case MainTab::Connections:
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
