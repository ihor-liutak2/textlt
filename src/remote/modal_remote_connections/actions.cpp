RemoteConnectionType RemoteConnectionsModalContent::CurrentType() const {
    return RemoteConnectionTypeFromString(type_value_);
}

bool RemoteConnectionsModalContent::IsCloudEditorActive() const {
    if (selected_tab_ == MainTab::Connections) {
        return false;
    }
    return TypeForTab(selected_tab_) == RemoteConnectionType::Dropbox;
}

RemoteConnectionConfig RemoteConnectionsModalContent::SelectedConnectionConfig() const {
    if (connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        return {};
    }
    return connections_[static_cast<size_t>(selected_connection_)];
}

RemoteConnectionConfig RemoteConnectionsModalContent::ActionConfig() const {
    if (selected_tab_ != MainTab::Connections) {
        return FormConfig();
    }
    return SelectedConnectionConfig();
}

void RemoteConnectionsModalContent::SelectTab(MainTab tab) {
    selected_tab_ = tab;
    RebuildFooterActions();
    help_active_ = false;
    help_layer_index_ = 0;
    output_.clear();

    if (tab == MainTab::Connections) {
        if (list_component_) {
            list_component_->TakeFocus();
        }
        SetStatus("Manage saved remote connections. Active: " + ActiveConnectionLabel());
        return;
    }

    ResetFormForType(TypeForTab(tab));
    if (name_input_) {
        name_input_->TakeFocus();
    }
    SetStatus("Editing " + TypeDisplayName(TypeForTab(tab)) + " connection.");
}

void RemoteConnectionsModalContent::SelectType(RemoteConnectionType type) {
    type_value_ = RemoteConnectionTypeToString(type);
    ApplyTypeDefaults(type);
    output_.clear();
    SetStatus("Selected " + TypeDisplayName(type) + " connection type.");
    help_active_ = false;
    help_layer_index_ = 0;
}

void RemoteConnectionsModalContent::ApplyTypeDefaults(RemoteConnectionType type) {
    if (remote_root_value_.empty()) {
        remote_root_value_ = "/";
    }
    switch (type) {
        case RemoteConnectionType::Sftp:
            if (port_value_.empty() || port_value_ == "0") {
                port_value_ = "22";
            }
            if (ssh_config_host_value_.empty()) {
                SyncSshConfigHostSelection();
            }
            break;
        case RemoteConnectionType::Dropbox:
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
            }
            break;
        case RemoteConnectionType::Ftps:
            if (port_value_.empty() || port_value_ == "0" || port_value_ == "22") {
                port_value_ = ftps_tls_mode_value_ == "implicit" ? "990" : "21";
            }
            if (ftps_tls_mode_value_.empty()) {
                ftps_tls_mode_value_ = "explicit";
            }
            break;
    }
}

void RemoteConnectionsModalContent::ResetFormForType(RemoteConnectionType type) {
    id_value_ = MakeRemoteConnectionId();
    name_value_ = DefaultNameForType(type);
    type_value_ = RemoteConnectionTypeToString(type);
    host_value_.clear();
    port_value_ = "22";
    user_value_.clear();
    password_value_.clear();
    remote_root_value_ = "/";
    auth_mode_value_.clear();
    identity_file_value_.clear();
    key_passphrase_value_.clear();
    known_hosts_file_value_.clear();
    ssh_config_host_value_.clear();
    token_file_value_.clear();
    app_key_value_.clear();
    app_secret_value_.clear();
    access_token_value_.clear();
    refresh_token_value_.clear();
    ftps_tls_mode_value_ = "explicit";
    ftps_passive_value_ = true;
    ApplyTypeDefaults(type);

    name_cursor_ = static_cast<int>(name_value_.size());
    host_cursor_ = 0;
    port_cursor_ = static_cast<int>(port_value_.size());
    user_cursor_ = 0;
    password_cursor_ = 0;
    remote_root_cursor_ = static_cast<int>(remote_root_value_.size());
    auth_mode_cursor_ = static_cast<int>(auth_mode_value_.size());
    identity_file_cursor_ = 0;
    key_passphrase_cursor_ = 0;
    known_hosts_file_cursor_ = 0;
    app_key_cursor_ = 0;
    app_secret_cursor_ = 0;
    access_token_cursor_ = 0;
    refresh_token_cursor_ = 0;
    ftps_tls_mode_cursor_ = static_cast<int>(ftps_tls_mode_value_.size());
}

void RemoteConnectionsModalContent::LoadSelectedIntoForm() {
    if (connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        return;
    }
    const RemoteConnectionConfig& config = connections_[selected_connection_];
    id_value_ = config.id;
    name_value_ = config.name;
    type_value_ = RemoteConnectionTypeToString(config.type);
    host_value_ = config.host;
    port_value_ = std::to_string(config.port <= 0 ? 22 : config.port);
    user_value_ = config.user;
    password_value_ = config.password;
    remote_root_value_ = config.remote_root.empty() ? "/" : config.remote_root;
    auth_mode_value_ = config.auth_mode.empty() ? "auto" : config.auth_mode;
    identity_file_value_ = config.identity_file;
    key_passphrase_value_ = config.key_passphrase;
    known_hosts_file_value_ = config.known_hosts_file;
    ssh_config_host_value_ = config.ssh_config_host;
    SelectSshConfigHostValue(ssh_config_host_value_);
    token_file_value_ = config.token_file;
    app_key_value_ = config.app_key;
    app_secret_value_ = config.app_secret;
    access_token_value_.clear();
    refresh_token_value_.clear();
    ftps_tls_mode_value_ = config.ftps_tls_mode.empty() ? "explicit" : config.ftps_tls_mode;
    ftps_passive_value_ = config.ftps_passive;
    ApplyTypeDefaults(config.type);

    name_cursor_ = static_cast<int>(name_value_.size());
    host_cursor_ = static_cast<int>(host_value_.size());
    port_cursor_ = static_cast<int>(port_value_.size());
    user_cursor_ = static_cast<int>(user_value_.size());
    password_cursor_ = static_cast<int>(password_value_.size());
    remote_root_cursor_ = static_cast<int>(remote_root_value_.size());
    auth_mode_cursor_ = static_cast<int>(auth_mode_value_.size());
    identity_file_cursor_ = static_cast<int>(identity_file_value_.size());
    key_passphrase_cursor_ = static_cast<int>(key_passphrase_value_.size());
    known_hosts_file_cursor_ = static_cast<int>(known_hosts_file_value_.size());
    app_key_cursor_ = static_cast<int>(app_key_value_.size());
    app_secret_cursor_ = static_cast<int>(app_secret_value_.size());
    access_token_cursor_ = 0;
    refresh_token_cursor_ = 0;
    ftps_tls_mode_cursor_ = static_cast<int>(ftps_tls_mode_value_.size());
}

void RemoteConnectionsModalContent::SaveFormToSelected() {
    if (!config_store_) {
        SetStatus("Remote config store is not available.", true);
        return;
    }
    RemoteConnectionConfig config = FormConfig();
    if (config.id.empty()) {
        config.id = MakeRemoteConnectionId();
        id_value_ = config.id;
    }
    if (config.name.empty()) {
        SetStatus("Connection name is required.", true);
        return;
    }
    if (config.type == RemoteConnectionType::Sftp && config.ssh_config_host.empty()) {
        SetStatus("SFTP connection needs an alias from ~/.ssh/config.", true);
        return;
    }
    if (config.type == RemoteConnectionType::Ftps && config.host.empty()) {
        SetStatus("FTPS connection needs Host.", true);
        return;
    }
    if (config.type == RemoteConnectionType::Dropbox && config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    config_store_->AddOrUpdate(config);
    if (config.type == RemoteConnectionType::Dropbox && !access_token_value_.empty()) {
        SaveCloudAccessToken();
        if (status_is_error_) {
            return;
        }
    }
    std::string error;
    if (!config_store_->Save(error)) {
        SetStatus(error, true);
        return;
    }

    connections_ = config_store_->Connections();
    connection_boxes_.resize(connections_.size());
    for (size_t index = 0; index < connections_.size(); ++index) {
        if (connections_[index].id == config.id) {
            selected_connection_ = static_cast<int>(index);
            break;
        }
    }
    LoadSelectedIntoForm();
    selected_tab_ = TabForType(config.type);
    SetStatus("Saved " + TypeDisplayName(config.type) + " connection: " + config.name);
}

void RemoteConnectionsModalContent::AddConnectionOfType(RemoteConnectionType type) {
    ResetFormForType(type);
    selected_tab_ = TabForType(type);
    RebuildFooterActions();
    selected_connection_ = -1;
    output_.clear();
    if (name_input_) {
        name_input_->TakeFocus();
    }
    SetStatus("Create " + TypeDisplayName(type) + " connection. Fill fields and press Save.");
}

void RemoteConnectionsModalContent::EditSelected() {
    if (connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        SetStatus("No saved connection selected.", true);
        return;
    }
    const RemoteConnectionConfig& selected = connections_[static_cast<size_t>(selected_connection_)];
    LoadSelectedIntoForm();
    selected_tab_ = TabForType(selected.type);
    RebuildFooterActions();
    output_.clear();
    if (name_input_) {
        name_input_->TakeFocus();
    }
    SetStatus("Editing " + TypeDisplayName(selected.type) + " connection: " + selected.name);
}

void RemoteConnectionsModalContent::SetSelectedActive() {
    if (!config_store_ || connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        SetStatus("No saved connection selected.", true);
        return;
    }
    const RemoteConnectionConfig& selected = connections_[static_cast<size_t>(selected_connection_)];
    if (selected.id.empty()) {
        SetStatus("Save the connection before setting it active.", true);
        return;
    }
    config_store_->SetActiveConnectionId(selected.id);
    std::string error;
    if (!config_store_->Save(error)) {
        SetStatus(error, true);
        return;
    }
    Reload();
    SetStatus("Active remote connection: " + ActiveConnectionLabel());
}

void RemoteConnectionsModalContent::DeleteSelected() {
    if (!config_store_ || connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        SetStatus("No connection selected.", true);
        return;
    }
    const std::string id = connections_[selected_connection_].id;
    const std::string name = connections_[selected_connection_].name;
    if (!config_store_->RemoveById(id)) {
        SetStatus("Connection was not removed.", true);
        return;
    }
    std::string error;
    if (!config_store_->Save(error)) {
        SetStatus(error, true);
        return;
    }
    Reload();
    SetStatus("Deleted connection: " + name);
}

void RemoteConnectionsModalContent::TestSelected() {
    RemoteConnectionConfig config = ActionConfig();
    if (config.id.empty() && config.name.empty()) {
        SetStatus("No connection selected for testing.", true);
        return;
    }
    if (config.type != RemoteConnectionType::Dropbox && config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    if (config.type == RemoteConnectionType::Dropbox) {
        RemoteDropboxProvider provider;
        std::string error;
        if (!provider.Connect(config, error)) {
            output_ = DescribeRemoteOAuthTokenStatus(config) + "\n" + error;
            SetStatus("Dropbox token is not ready.", true);
            return;
        }
        std::string output;
        if (!provider.TestConnection(output, error)) {
            output_ = error;
            SetStatus("Dropbox connection test failed.", true);
            return;
        }
        output_ = output;
        SetStatus("Dropbox connection test succeeded.");
        return;
    }

    if (config.type == RemoteConnectionType::Ftps) {
        RemoteFtpsProvider provider;
        std::string error;
        if (!provider.Connect(config, error)) {
            output_.clear();
            SetStatus(error, true);
            return;
        }
        std::string output;
        if (!provider.TestConnection(output, error)) {
            output_ = error;
            SetStatus("FTPS connection test failed.", true);
            return;
        }
        output_ = output;
        SetStatus("FTPS connection test succeeded.");
        return;
    }

    RemoteSftpProvider provider;
    std::string error;
    if (!provider.Connect(config, error)) {
        output_.clear();
        SetStatus(error, true);
        return;
    }
    std::string output;
    if (!provider.TestConnection(output, error)) {
        output_ = error;
        SetStatus("Connection test failed.", true);
        return;
    }
    output_ = output;
    SetStatus("Connection test succeeded.");
}

void RemoteConnectionsModalContent::SaveCloudAccessToken() {
    RemoteConnectionConfig config = FormConfig();
    if (config.type != RemoteConnectionType::Dropbox) {
        SetStatus("SFTP does not use OAuth access tokens.", true);
        return;
    }
    if (config.name.empty()) {
        SetStatus("Connection name is required before saving a token.", true);
        return;
    }
    if (access_token_value_.empty() && refresh_token_value_.empty()) {
        SetStatus("Paste an access token or refresh token first.", true);
        return;
    }
    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    RemoteOAuthToken token;
    token.provider = RemoteTokenProviderName(config.type);
    token.display_name = config.name;
    token.access_token = access_token_value_;
    token.refresh_token = refresh_token_value_;
    token.token_type = "Bearer";

    RemoteOAuthTokenStore store(ExpandRemoteUserPath(config.token_file));
    std::string error;
    if (!store.Save(token, error)) {
        output_ = error;
        SetStatus("Cannot save access token.", true);
        return;
    }
    access_token_value_.clear();
    refresh_token_value_.clear();
    access_token_cursor_ = 0;
    refresh_token_cursor_ = 0;
    output_ = DescribeRemoteOAuthTokenStatus(config);
    SetStatus(TypeDisplayName(config.type) + " OAuth token saved to token file.");
}

void RemoteConnectionsModalContent::PrepareTokenFile() {
    RemoteConnectionConfig config = ActionConfig();
    if (config.type != RemoteConnectionType::Dropbox) {
        output_ = "SFTP uses OpenSSH config, keys, and ssh-agent and does not need an OAuth token file.";
        SetStatus("Token files are only used by Dropbox.");
        return;
    }
    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    const std::filesystem::path token_path = ExpandRemoteUserPath(config.token_file);
    RemoteOAuthTokenStore token_store(token_path);
    RemoteOAuthToken token;
    std::string error;
    if (token_store.Exists()) {
        if (!token_store.Load(token, error)) {
            output_ = error;
            SetStatus("Token file is invalid.", true);
            return;
        }
        output_ = DescribeRemoteOAuthTokenStatus(config);
        SetStatus("Token file already exists.");
        return;
    }

    token.provider = RemoteTokenProviderName(config.type);
    token.display_name = config.name;
    token.token_type = "Bearer";
    if (!token_store.Save(token, error)) {
        output_ = error;
        SetStatus("Cannot create token file.", true);
        return;
    }

    output_ = "Created placeholder token file:\n" + token_path.string() +
        "\nPaste an access token into the provider tab and press Save Token.";
    SetStatus("Token file prepared for " + TypeDisplayName(config.type) + ".");
}

void RemoteConnectionsModalContent::OpenHelp() {
    if (selected_tab_ != MainTab::Sftp && selected_tab_ != MainTab::Ftps &&
        selected_tab_ != MainTab::Dropbox) {
        return;
    }
    help_active_ = true;
    help_layer_index_ = 1;
    if (help_close_button_) {
        help_close_button_->TakeFocus();
    }
}

void RemoteConnectionsModalContent::RebuildFooterActions() {
    if (!footer_actions_container_) {
        return;
    }

    footer_actions_container_->DetachAllChildren();
    const auto add = [this](const ftxui::Component& button) {
        if (button) {
            footer_actions_container_->Add(button);
        }
    };

    if (selected_tab_ == MainTab::Connections) {
        add(edit_button_);
        add(set_active_button_);
        add(test_button_);
        add(delete_button_);
        add(reload_button_);
        add(copy_message_button_);
    } else {
        if (selected_tab_ == MainTab::Sftp || selected_tab_ == MainTab::Ftps ||
            selected_tab_ == MainTab::Dropbox) {
            add(help_button_);
        }
        add(new_button_);
        add(save_button_);
        if (selected_tab_ == MainTab::Dropbox) {
            add(save_token_button_);
        }
        add(test_button_);
    }
    add(close_button_);
}

void RemoteConnectionsModalContent::CloseHelp() {
    help_active_ = false;
    help_layer_index_ = 0;
}

void RemoteConnectionsModalContent::CopyHelpUrl() {
    if (write_clipboard_) {
        write_clipboard_("https://www.dropbox.com/developers/apps");
        SetStatus("Copied Dropbox developer apps URL to clipboard.");
    } else {
        SetStatus("Clipboard is not available.", true);
    }
}

void RemoteConnectionsModalContent::CopyActionMessage() {
    if (!write_clipboard_) {
        SetStatus("Clipboard is not available.", true);
        return;
    }
    write_clipboard_(ActionMessageText());
}
