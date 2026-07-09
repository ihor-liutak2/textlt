RemoteConnectionType RemoteConnectionsModalContent::CurrentType() const {
    return RemoteConnectionTypeFromString(type_value_);
}

bool RemoteConnectionsModalContent::IsCloudEditorActive() const {
    if (selected_tab_ == MainTab::Connections) {
        return false;
    }
    return IsCloudRemoteConnectionType(TypeForTab(selected_tab_));
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
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
    output_.clear();

    if (tab == MainTab::Connections) {
        if (list_component_) {
            list_component_->TakeFocus();
        }
        SetStatus("Manage saved remote connections. Active: " + ActiveConnectionLabel());
        return;
    }

    const RemoteConnectionType type = TypeForTab(tab);
    ResetFormForType(type);
    if (name_input_) {
        name_input_->TakeFocus();
    }
    SetStatus("Editing " + TypeDisplayName(type) + ".");
}

void RemoteConnectionsModalContent::SelectType(RemoteConnectionType type) {
    type_value_ = RemoteConnectionTypeToString(type);
    ApplyTypeDefaults(type);
    output_.clear();
    SetStatus("Selected " + TypeDisplayName(type) + " connection type.");
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
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
            if (auth_mode_value_.empty()) {
                auth_mode_value_ = "auto";
            }
            break;
        case RemoteConnectionType::GoogleDrive:
            if (scope_value_.empty()) {
                scope_value_ = "https://www.googleapis.com/auth/drive.file";
            }
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
            }
            break;
        case RemoteConnectionType::MicrosoftDrive:
            if (tenant_id_value_.empty()) {
                tenant_id_value_ = "common";
            }
            if (scope_value_.empty()) {
                scope_value_ = "offline_access Files.ReadWrite";
            }
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
            }
            break;
        case RemoteConnectionType::Dropbox:
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
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
    auth_mode_value_ = "auto";
    identity_file_value_.clear();
    key_passphrase_value_.clear();
    known_hosts_file_value_.clear();
    ssh_config_host_value_.clear();
    client_id_value_.clear();
    client_secret_value_.clear();
    tenant_id_value_.clear();
    token_file_value_.clear();
    root_folder_id_value_.clear();
    site_id_value_.clear();
    drive_id_value_.clear();
    app_key_value_.clear();
    app_secret_value_.clear();
    access_token_value_.clear();
    refresh_token_value_.clear();
    scope_value_.clear();
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
    ssh_config_host_cursor_ = 0;
    client_id_cursor_ = 0;
    client_secret_cursor_ = 0;
    tenant_id_cursor_ = static_cast<int>(tenant_id_value_.size());
    root_folder_id_cursor_ = 0;
    site_id_cursor_ = 0;
    drive_id_cursor_ = 0;
    app_key_cursor_ = 0;
    app_secret_cursor_ = 0;
    access_token_cursor_ = 0;
    refresh_token_cursor_ = 0;
    scope_cursor_ = static_cast<int>(scope_value_.size());
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
    client_id_value_ = config.client_id;
    client_secret_value_ = config.client_secret;
    tenant_id_value_ = config.tenant_id;
    token_file_value_ = config.token_file;
    root_folder_id_value_ = config.root_folder_id;
    site_id_value_ = config.site_id;
    drive_id_value_ = config.drive_id;
    app_key_value_ = config.app_key;
    app_secret_value_ = config.app_secret;
    scope_value_ = config.scope;
    access_token_value_.clear();
    refresh_token_value_.clear();
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
    ssh_config_host_cursor_ = static_cast<int>(ssh_config_host_value_.size());
    client_id_cursor_ = static_cast<int>(client_id_value_.size());
    client_secret_cursor_ = static_cast<int>(client_secret_value_.size());
    tenant_id_cursor_ = static_cast<int>(tenant_id_value_.size());
    root_folder_id_cursor_ = static_cast<int>(root_folder_id_value_.size());
    site_id_cursor_ = static_cast<int>(site_id_value_.size());
    drive_id_cursor_ = static_cast<int>(drive_id_value_.size());
    app_key_cursor_ = static_cast<int>(app_key_value_.size());
    app_secret_cursor_ = static_cast<int>(app_secret_value_.size());
    access_token_cursor_ = 0;
    refresh_token_cursor_ = 0;
    scope_cursor_ = static_cast<int>(scope_value_.size());
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
    if (config.type == RemoteConnectionType::Sftp &&
        config.ssh_config_host.empty() && config.host.empty()) {
        SetStatus("SFTP connection needs Host or SSH config alias.", true);
        return;
    }
    if (IsCloudRemoteConnectionType(config.type) && config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    config_store_->AddOrUpdate(config);
    if (IsCloudRemoteConnectionType(config.type) && !access_token_value_.empty()) {
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

    if (config.type == RemoteConnectionType::Sftp) {
        SetStatus("Saved connection: " + config.name);
    } else {
        SetStatus("Saved " + TypeDisplayName(config.type) + " configuration: " + config.name);
    }
}

void RemoteConnectionsModalContent::AddConnectionOfType(RemoteConnectionType type) {
    ResetFormForType(type);
    selected_tab_ = TabForType(type);
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
    output_.clear();
    if (name_input_) {
        name_input_->TakeFocus();
    }
    SetStatus("Editing " + TypeDisplayName(selected.type) + ": " + selected.name);
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
    if (config.type != RemoteConnectionType::Sftp && config.token_file.empty()) {
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

    if (config.type == RemoteConnectionType::GoogleDrive) {
        RemoteGoogleDriveProvider provider;
        std::string error;
        if (!provider.Connect(config, error)) {
            output_ = DescribeRemoteOAuthTokenStatus(config) + "\n" + error;
            SetStatus("Google Drive token is not ready.", true);
            return;
        }
        std::string output;
        if (!provider.TestConnection(output, error)) {
            output_ = error;
            SetStatus("Google Drive connection test failed.", true);
            return;
        }
        output_ = output;
        SetStatus("Google Drive connection test succeeded.");
        return;
    }

    if (config.type == RemoteConnectionType::MicrosoftDrive) {
        RemoteMicrosoftDriveProvider provider;
        std::string error;
        if (!provider.Connect(config, error)) {
            output_ = DescribeRemoteOAuthTokenStatus(config) + "\n" + error;
            SetStatus("Microsoft token is not ready.", true);
            return;
        }
        std::string output;
        if (!provider.TestConnection(output, error)) {
            output_ = error;
            SetStatus("Microsoft connection test failed.", true);
            return;
        }
        output_ = output;
        SetStatus("Microsoft connection test succeeded.");
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
    if (!IsCloudRemoteConnectionType(config.type)) {
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
    token.scope = scope_value_;

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
    if (!IsCloudRemoteConnectionType(config.type)) {
        output_ = "SFTP uses SSH keys / ssh-agent / ~/.ssh/config and does not need an OAuth token file.";
        SetStatus("Token files are only used by Google, Microsoft, and Dropbox.");
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

void RemoteConnectionsModalContent::AuthorizeConnection() {
    RemoteConnectionConfig config = FormConfig();
    if (config.type != RemoteConnectionType::GoogleDrive &&
        config.type != RemoteConnectionType::MicrosoftDrive) {
        SetStatus("Authorization is only available for Google and Microsoft. Paste a Dropbox token manually.", true);
        return;
    }

    OAuthFlowConfig oauth;

    switch (config.type) {
        case RemoteConnectionType::GoogleDrive:
            oauth.authorize_url = "https://accounts.google.com/o/oauth2/v2/auth";
            oauth.token_url = "https://oauth2.googleapis.com/token";
            oauth.client_id = config.client_id;
            oauth.client_secret = config.client_secret;
            oauth.scope = config.scope.empty() ? "https://www.googleapis.com/auth/drive.file" : config.scope;
            break;
        case RemoteConnectionType::MicrosoftDrive: {
            const std::string tenant = config.tenant_id.empty() ? "common" : config.tenant_id;
            oauth.authorize_url = "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/authorize";
            oauth.token_url = "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/token";
            oauth.client_id = config.client_id;
            oauth.client_secret = config.client_secret;
            oauth.scope = config.scope.empty() ? "offline_access Files.ReadWrite" : config.scope;
            break;
        }
        default:
            return;
    }

    if (oauth.client_id.empty() || oauth.client_secret.empty()) {
        SetStatus("Fill in Client ID and Client secret first.", true);
        return;
    }

    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    OAuthFlow flow;
    std::string auth_url = flow.BuildAuthorizeUrl(oauth);

    if (write_clipboard_) {
        write_clipboard_(auth_url);
    }

    pending_oauth_config_ = oauth;
    authorize_pending_ = true;
    authorize_layer_index_ = 2;
    redirect_url_value_.clear();
    if (redirect_url_input_) {
        redirect_url_input_->TakeFocus();
    }

    output_ = "Authorization URL copied to clipboard.\n"
              "Open it in your browser, authorize access, then paste the redirect URL from the address bar.\n"
              "Redirect URI: " + oauth.redirect_uri + "\n"
              "URL: " + auth_url;
    SetStatus("Paste the redirect URL from your browser and press Submit.");
}

void RemoteConnectionsModalContent::SubmitRedirectUrl() {
    if (!authorize_pending_) {
        return;
    }

    const std::string& input = redirect_url_value_;

    const bool looks_like_url = input.find("://") != std::string::npos;

    authorize_pending_ = false;
    authorize_layer_index_ = 0;

    RemoteConnectionConfig config = FormConfig();
    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    std::string display_name = config.name;
    const std::string provider_name = RemoteTokenProviderName(config.type);

    if (looks_like_url) {
        OAuthFlow flow;
        std::string code = flow.ExtractCodeFromRedirectUrl(input);
        if (code.empty()) {
            SetStatus("Could not extract authorization code from the URL. Paste the full redirect URL.", true);
            return;
        }

        SetStatus("Exchanging authorization code for tokens...");

        OAuthTokenExchangeResult exchange = flow.ExchangeCodeForToken(pending_oauth_config_, code);
        if (!exchange.ok) {
            output_ = exchange.error;
            SetStatus("Token exchange failed: " + exchange.error, true);
            return;
        }

        if (!flow.SaveToken(pending_oauth_config_, provider_name,
                            display_name, exchange, config.token_file)) {
            SetStatus("Failed to save token file.", true);
            return;
        }
    } else {
        if (input.empty()) {
            SetStatus("Paste a redirect URL or a raw access token.", true);
            return;
        }

        RemoteOAuthToken token;
        token.provider = provider_name;
        token.display_name = display_name;
        token.access_token = input;
        token.token_type = "Bearer";
        token.scope = config.scope;

        RemoteOAuthTokenStore store(ExpandRemoteUserPath(config.token_file));
        std::string error;
        if (!store.Save(token, error)) {
            output_ = error;
            SetStatus("Failed to save token file.", true);
            return;
        }
    }

    output_ = DescribeRemoteOAuthTokenStatus(config);
    SetStatus("Authorization complete for " + TypeDisplayName(config.type) + ".");
}

void RemoteConnectionsModalContent::OpenHelp() {
    help_active_ = true;
    help_layer_index_ = 1;
    if (help_close_button_) {
        help_close_button_->TakeFocus();
    }
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
