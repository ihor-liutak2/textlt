void RemoteConnectionsModalContent::SelectConnection(int index) {
    if (connections_.empty()) {
        selected_connection_ = 0;
        return;
    }
    selected_connection_ = std::clamp(index, 0, static_cast<int>(connections_.size()) - 1);
    LoadSelectedIntoForm();
}

RemoteConnectionConfig RemoteConnectionsModalContent::FormConfig() const {
    RemoteConnectionConfig config;
    config.id = id_value_;
    config.name = name_value_;
    config.type = CurrentType();
    config.host = host_value_;
    config.port = ParsePort(port_value_);
    config.user = user_value_;
    config.remote_root = NormalizeRemoteDirectory(remote_root_value_);
    config.identity_file = identity_file_value_;
    config.ssh_config_host = ssh_config_host_value_;
    config.account_label = account_label_value_;
    config.client_id = client_id_value_;
    config.client_secret = client_secret_value_;
    config.tenant_id = tenant_id_value_;
    config.token_file = token_file_value_;
    config.root_folder_id = root_folder_id_value_;
    config.site_id = site_id_value_;
    config.drive_id = drive_id_value_;
    config.app_key = app_key_value_;
    config.app_secret = app_secret_value_;
    return config;
}

std::string RemoteConnectionsModalContent::SuggestedTokenFile(RemoteConnectionType type) const {
    std::string stable_name = account_label_value_;
    if (stable_name.empty()) {
        stable_name = name_value_;
    }
    if (stable_name.empty()) {
        stable_name = id_value_;
    }
    return DefaultRemoteTokenPath(type, stable_name).string();
}

void RemoteConnectionsModalContent::SetStatus(std::string status, bool is_error) {
    status_ = std::move(status);
    status_is_error_ = is_error;
}

ftxui::Element RemoteConnectionsModalContent::RenderHelpOverlay() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    rows.push_back(text(" Dropbox help ") | bold | color(theme.modal_accent));
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" To connect to Dropbox:") | color(theme.modal_text_color));
    rows.push_back(text(" 1. Go to the Dropbox Developer Console.") | color(theme.modal_text_color));
    rows.push_back(text(" 2. Create a new app with full access.") | color(theme.modal_text_color));
    rows.push_back(text(" 3. Copy the App key and App secret.") | color(theme.modal_text_color));
    rows.push_back(text(" 4. Paste them into the fields above.") | color(theme.modal_text_color));
    rows.push_back(text(" 5. Press Token to generate the token file.") | color(theme.modal_text_color));
    rows.push_back(text(" 6. Press Authorize to paste a token or redirect URL.") | color(theme.modal_text_color));
    rows.push_back(text("    You can paste a generated access token from the app console.") | color(theme.modal_text_color));
    rows.push_back(text(" 7. Press Test to verify the connection.") | color(theme.modal_text_color));
    rows.push_back(text(""));
    rows.push_back(text(" Dropbox app console: dropbox.com/developers/apps") |
                   color(theme.modal_text_color));
    rows.push_back(text(""));
    rows.push_back(hbox({
        filler(),
        copy_url_button_ ? copy_url_button_->Render() : text(""),
        text(" "),
        help_close_button_ ? help_close_button_->Render() : text(""),
    }));
    return vbox(std::move(rows)) |
        size(WIDTH, LESS_THAN, 60) |
        border |
        bgcolor(theme.modal_background) |
        color(theme.modal_border) |
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
    modal_->SetFooterText("Choose SFTP, Google, Microsoft, or Dropbox. SFTP, Dropbox, Google Drive, and Microsoft file operations are active now.");
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
