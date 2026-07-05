RemoteConnectionType RemoteConnectionsModalContent::CurrentType() const {
    return RemoteConnectionTypeFromString(type_value_);
}

void RemoteConnectionsModalContent::SelectType(RemoteConnectionType type) {
    type_value_ = RemoteConnectionTypeToString(type);
    ApplyTypeDefaults(type);
    output_.clear();
    SetStatus("Editing " + TypeDisplayName(type) + " connection fields.");
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
            break;
        case RemoteConnectionType::GoogleDrive:
            if (token_file_value_.empty()) {
                token_file_value_ = SuggestedTokenFile(type);
            }
            break;
        case RemoteConnectionType::MicrosoftDrive:
            if (tenant_id_value_.empty()) {
                tenant_id_value_ = "common";
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
    remote_root_value_ = config.remote_root.empty() ? "/" : config.remote_root;
    identity_file_value_ = config.identity_file;
    ssh_config_host_value_ = config.ssh_config_host;
    account_label_value_ = config.account_label;
    client_id_value_ = config.client_id;
    client_secret_value_ = config.client_secret;
    tenant_id_value_ = config.tenant_id;
    token_file_value_ = config.token_file;
    root_folder_id_value_ = config.root_folder_id;
    site_id_value_ = config.site_id;
    drive_id_value_ = config.drive_id;
    app_key_value_ = config.app_key;
    app_secret_value_ = config.app_secret;
    ApplyTypeDefaults(config.type);

    name_cursor_ = static_cast<int>(name_value_.size());
    host_cursor_ = static_cast<int>(host_value_.size());
    port_cursor_ = static_cast<int>(port_value_.size());
    user_cursor_ = static_cast<int>(user_value_.size());
    remote_root_cursor_ = static_cast<int>(remote_root_value_.size());
    identity_file_cursor_ = static_cast<int>(identity_file_value_.size());
    ssh_config_host_cursor_ = static_cast<int>(ssh_config_host_value_.size());
    account_label_cursor_ = static_cast<int>(account_label_value_.size());
    client_id_cursor_ = static_cast<int>(client_id_value_.size());
    client_secret_cursor_ = static_cast<int>(client_secret_value_.size());
    tenant_id_cursor_ = static_cast<int>(tenant_id_value_.size());
    root_folder_id_cursor_ = static_cast<int>(root_folder_id_value_.size());
    site_id_cursor_ = static_cast<int>(site_id_value_.size());
    drive_id_cursor_ = static_cast<int>(drive_id_value_.size());
    app_key_cursor_ = static_cast<int>(app_key_value_.size());
    app_secret_cursor_ = static_cast<int>(app_secret_value_.size());
}

void RemoteConnectionsModalContent::SaveFormToSelected() {
    if (!config_store_) {
        SetStatus("Remote config store is not available.", true);
        return;
    }
    RemoteConnectionConfig config = FormConfig();
    if (config.id.empty()) {
        config.id = MakeRemoteConnectionId();
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
    std::string error;
    if (!config_store_->Save(error)) {
        SetStatus(error, true);
        return;
    }
    Reload();
    for (size_t index = 0; index < connections_.size(); ++index) {
        if (connections_[index].id == config.id) {
            SelectConnection(static_cast<int>(index));
            break;
        }
    }
    if (config.type == RemoteConnectionType::Sftp) {
        SetStatus("Saved connection: " + config.name);
    } else {
        SetStatus("Saved " + TypeDisplayName(config.type) + " configuration: " + config.name);
    }
}

void RemoteConnectionsModalContent::AddConnection() {
    const RemoteConnectionType type = CurrentType();
    id_value_ = MakeRemoteConnectionId();
    name_value_ = DefaultNameForType(type);
    type_value_ = RemoteConnectionTypeToString(type);
    host_value_.clear();
    port_value_ = "22";
    user_value_.clear();
    remote_root_value_ = "/";
    identity_file_value_.clear();
    ssh_config_host_value_.clear();
    account_label_value_.clear();
    client_id_value_.clear();
    client_secret_value_.clear();
    tenant_id_value_.clear();
    token_file_value_.clear();
    root_folder_id_value_.clear();
    site_id_value_.clear();
    drive_id_value_.clear();
    app_key_value_.clear();
    app_secret_value_.clear();
    ApplyTypeDefaults(type);
    SetStatus("Fill " + TypeDisplayName(type) + " fields and press Save.");
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
    RemoteConnectionConfig config = FormConfig();
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

    if (config.type != RemoteConnectionType::Sftp) {
        output_ = DescribeRemoteOAuthTokenStatus(config) +
            "\nOAuth login and REST file operations for this provider will use this token file in a later API patch.";
        SetStatus("Checked token file for " + TypeDisplayName(config.type) + ".");
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

void RemoteConnectionsModalContent::PrepareTokenFile() {
    RemoteConnectionConfig config = FormConfig();
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
    token.account_label = config.account_label.empty() ? config.name : config.account_label;
    token.token_type = "Bearer";
    if (!token_store.Save(token, error)) {
        output_ = error;
        SetStatus("Cannot create token file.", true);
        return;
    }

    output_ = "Created placeholder token file:\n" + token_path.string() +
        "\nThe next OAuth/API patch will fill access_token and refresh_token.";
    SetStatus("Token file prepared for " + TypeDisplayName(config.type) + ".");
}

void RemoteConnectionsModalContent::AuthorizeConnection() {
    RemoteConnectionConfig config = FormConfig();
    if (!IsCloudRemoteConnectionType(config.type)) {
        SetStatus("Authorization is only available for Google, Microsoft, and Dropbox.", true);
        return;
    }

    OAuthFlowConfig oauth;

    switch (config.type) {
        case RemoteConnectionType::Dropbox:
            oauth.authorize_url = "https://www.dropbox.com/oauth2/authorize";
            oauth.token_url = "https://api.dropboxapi.com/oauth2/token";
            oauth.client_id = config.app_key;
            oauth.client_secret = config.app_secret;
            oauth.scope = "account_info.read files.metadata.read files.content.read files.content.write";
            break;
        case RemoteConnectionType::GoogleDrive:
            oauth.authorize_url = "https://accounts.google.com/o/oauth2/v2/auth";
            oauth.token_url = "https://oauth2.googleapis.com/token";
            oauth.client_id = config.client_id;
            oauth.client_secret = config.client_secret;
            oauth.scope = "https://www.googleapis.com/auth/drive";
            break;
        case RemoteConnectionType::MicrosoftDrive: {
            const std::string tenant = config.tenant_id.empty() ? "common" : config.tenant_id;
            oauth.authorize_url = "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/authorize";
            oauth.token_url = "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/token";
            oauth.client_id = config.client_id;
            oauth.client_secret = config.client_secret;
            oauth.scope = "offline_access Files.ReadWrite.All";
            break;
        }
        default:
            return;
    }

    if (oauth.client_id.empty() || oauth.client_secret.empty()) {
        SetStatus("Fill in the credentials (app key/secret or client id/secret) first.", true);
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
              "Steps:\n"
              "1. In Dropbox app settings, add redirect URI:\n"
              "  " + oauth.redirect_uri + "\n"
              "2. Add scopes: account_info.read files.metadata.read\n"
              "   files.content.read files.content.write\n"
              "3. Generate access token on the Dropbox site\n"
              "4. Open the URL below in your browser, authorize,\n"
              "   and paste the redirect URL from the address bar.\n"
              "URL: " + auth_url;
    SetStatus("Paste the redirect URL from your browser and press Submit.");
}

void RemoteConnectionsModalContent::SubmitRedirectUrl() {
    if (!authorize_pending_) {
        return;
    }

    const std::string& input = redirect_url_value_;

    // Check if the input looks like a raw access token (not a URL).
    // Dropbox generated access tokens are short strings like "sl.xxxxx..."
    // with no scheme or query parameters.  Authorization redirect URLs contain
    // "://".
    const bool looks_like_url = input.find("://") != std::string::npos;

    authorize_pending_ = false;
    authorize_layer_index_ = 0;

    RemoteConnectionConfig config = FormConfig();
    if (config.token_file.empty()) {
        config.token_file = SuggestedTokenFile(config.type);
        token_file_value_ = config.token_file;
    }

    std::string account = config.account_label.empty() ? config.name : config.account_label;
    const std::string provider_name = RemoteTokenProviderName(config.type);

    if (looks_like_url) {
        // Normal OAuth redirect URL — extract the authorization code and exchange it.
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
                            account, exchange, config.token_file)) {
            SetStatus("Failed to save token file.", true);
            return;
        }
    } else {
        // Treat the input as a raw access token (e.g. from the Dropbox app console).
        if (input.empty()) {
            SetStatus("Paste a redirect URL or a raw access token.", true);
            return;
        }

        RemoteOAuthToken token;
        token.provider = provider_name;
        token.account_label = account;
        token.access_token = input;
        token.token_type = "Bearer";

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
