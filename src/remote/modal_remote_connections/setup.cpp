RemoteConnectionsModalContent::RemoteConnectionsModalContent(
    const Theme* theme,
    RemoteConfigStore* config_store,
    WriteClipboardCallback write_clipboard,
    CloseCallback on_close)
    : theme_(theme),
      config_store_(config_store),
      write_clipboard_(std::move(write_clipboard)),
      on_close_(std::move(on_close)) {
    auto input_transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RemoteDialogInputTransform(theme, std::move(state));
    };

    ftxui::InputOption base_option;
    base_option.multiline = false;
    base_option.transform = input_transform;

    ftxui::InputOption name_option = base_option;
    name_option.cursor_position = &name_cursor_;
    name_input_ = ftxui::Input(&name_value_, "connection name", name_option);

    ftxui::InputOption host_option = base_option;
    host_option.cursor_position = &host_cursor_;
    host_input_ = ftxui::Input(&host_value_, "host", host_option);

    ftxui::InputOption port_option = base_option;
    port_option.cursor_position = &port_cursor_;
    port_input_ = ftxui::Input(&port_value_, "22", port_option);

    ftxui::InputOption user_option = base_option;
    user_option.cursor_position = &user_cursor_;
    user_input_ = ftxui::Input(&user_value_, "user", user_option);

    ftxui::InputOption password_option = base_option;
    password_option.cursor_position = &password_cursor_;
    password_input_ = ftxui::Input(&password_value_, "password", password_option);

    ftxui::InputOption root_option = base_option;
    root_option.cursor_position = &remote_root_cursor_;
    remote_root_input_ = ftxui::Input(&remote_root_value_, "/remote/path", root_option);

    ftxui::InputOption auth_mode_option = base_option;
    auth_mode_option.cursor_position = &auth_mode_cursor_;
    auth_mode_input_ = ftxui::Input(&auth_mode_value_, "auto / password / private_key / agent / ssh_config", auth_mode_option);

    ftxui::InputOption identity_option = base_option;
    identity_option.cursor_position = &identity_file_cursor_;
    identity_file_input_ = ftxui::Input(&identity_file_value_, "~/.ssh/id_ed25519", identity_option);

    ftxui::InputOption key_passphrase_option = base_option;
    key_passphrase_option.cursor_position = &key_passphrase_cursor_;
    key_passphrase_input_ = ftxui::Input(&key_passphrase_value_, "private key passphrase", key_passphrase_option);

    ftxui::InputOption known_hosts_option = base_option;
    known_hosts_option.cursor_position = &known_hosts_file_cursor_;
    known_hosts_file_input_ = ftxui::Input(&known_hosts_file_value_, "~/.ssh/known_hosts", known_hosts_option);

    ftxui::InputOption ssh_alias_option = base_option;
    ssh_alias_option.cursor_position = &ssh_config_host_cursor_;
    ssh_config_host_input_ = ftxui::Input(&ssh_config_host_value_, "Host alias from ~/.ssh/config", ssh_alias_option);

    ftxui::InputOption client_id_option = base_option;
    client_id_option.cursor_position = &client_id_cursor_;
    client_id_input_ = ftxui::Input(&client_id_value_, "OAuth client id", client_id_option);

    ftxui::InputOption client_secret_option = base_option;
    client_secret_option.cursor_position = &client_secret_cursor_;
    client_secret_input_ = ftxui::Input(&client_secret_value_, "OAuth client secret", client_secret_option);

    ftxui::InputOption tenant_option = base_option;
    tenant_option.cursor_position = &tenant_id_cursor_;
    tenant_id_input_ = ftxui::Input(&tenant_id_value_, "common / organizations / tenant id", tenant_option);

    ftxui::InputOption root_folder_option = base_option;
    root_folder_option.cursor_position = &root_folder_id_cursor_;
    root_folder_id_input_ = ftxui::Input(&root_folder_id_value_, "root folder id", root_folder_option);

    ftxui::InputOption site_option = base_option;
    site_option.cursor_position = &site_id_cursor_;
    site_id_input_ = ftxui::Input(&site_id_value_, "SharePoint site id", site_option);

    ftxui::InputOption drive_option = base_option;
    drive_option.cursor_position = &drive_id_cursor_;
    drive_id_input_ = ftxui::Input(&drive_id_value_, "OneDrive / SharePoint drive id", drive_option);

    ftxui::InputOption app_key_option = base_option;
    app_key_option.cursor_position = &app_key_cursor_;
    app_key_input_ = ftxui::Input(&app_key_value_, "Dropbox app key", app_key_option);

    ftxui::InputOption app_secret_option = base_option;
    app_secret_option.cursor_position = &app_secret_cursor_;
    app_secret_input_ = ftxui::Input(&app_secret_value_, "Dropbox app secret", app_secret_option);

    ftxui::InputOption access_token_option = base_option;
    access_token_option.cursor_position = &access_token_cursor_;
    access_token_input_ = ftxui::Input(&access_token_value_, "paste access token", access_token_option);

    ftxui::InputOption refresh_token_option = base_option;
    refresh_token_option.cursor_position = &refresh_token_cursor_;
    refresh_token_input_ = ftxui::Input(&refresh_token_value_, "paste refresh token", refresh_token_option);

    ftxui::InputOption scope_option = base_option;
    scope_option.cursor_position = &scope_cursor_;
    scope_input_ = ftxui::Input(&scope_value_, "OAuth scope", scope_option);

    delete_button_ = MakeTextButton("Delete", [this] { DeleteSelected(); },
        ButtonRole::Danger, ButtonVariant::AccentEdges, "!");
    save_button_ = MakeTextButton("Save", [this] { SaveFormToSelected(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    test_button_ = MakeTextButton("Test", [this] { TestSelected(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "▶");
    set_active_button_ = MakeTextButton("Set Active", [this] { SetSelectedActive(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    save_token_button_ = MakeTextButton("Save Token", [this] { SaveCloudAccessToken(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    edit_button_ = MakeTextButton("Edit", [this] { EditSelected(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    connections_tab_button_ = MakeTabButton("Connections", MainTab::Connections);
    sftp_tab_button_ = MakeTabButton("SFTP", MainTab::Sftp);
    google_tab_button_ = MakeTabButton("Google Drive", MainTab::GoogleDrive);
    microsoft_tab_button_ = MakeTabButton("Microsoft", MainTab::MicrosoftDrive);
    dropbox_tab_button_ = MakeTabButton("Dropbox", MainTab::Dropbox);
    reload_button_ = MakeTextButton("Reload", [this] { Reload(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "⟳");
    help_button_ = MakeTextButton("Info", [this] { OpenHelp(); },
        ButtonRole::Utility, ButtonVariant::AccentEdges, "i");
    authorize_button_ = MakeTextButton("Authorize", [this] { AuthorizeConnection(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);

    auto redirect_option = base_option;
    redirect_option.cursor_position = &redirect_url_cursor_;
    redirect_url_input_ = ftxui::Input(&redirect_url_value_, "paste redirect URL here", redirect_option);
    submit_redirect_button_ = MakeTextButton("Submit", [this] { SubmitRedirectUrl(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    cancel_authorize_button_ = MakeTextButton("Cancel", [this] {
        authorize_pending_ = false;
        authorize_layer_index_ = 0;
        SetStatus("Authorization cancelled.");
    }, ButtonRole::Cancel, ButtonVariant::AccentEdges);
    close_button_ = MakeTextButton("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    }, ButtonRole::Cancel, ButtonVariant::AccentEdges);

    list_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return RenderConnectionList(); }),
        [this](ftxui::Event event) { return HandleListEvent(std::move(event)); });

    ftxui::Components tab_buttons;
    tab_buttons.push_back(connections_tab_button_);
    tab_buttons.push_back(sftp_tab_button_);
    tab_buttons.push_back(google_tab_button_);
    tab_buttons.push_back(microsoft_tab_button_);
    tab_buttons.push_back(dropbox_tab_button_);

    ftxui::Components editor_fields;
    editor_fields.push_back(name_input_);
    editor_fields.push_back(host_input_);
    editor_fields.push_back(port_input_);
    editor_fields.push_back(user_input_);
    editor_fields.push_back(password_input_);
    editor_fields.push_back(remote_root_input_);
    editor_fields.push_back(auth_mode_input_);
    editor_fields.push_back(identity_file_input_);
    editor_fields.push_back(key_passphrase_input_);
    editor_fields.push_back(known_hosts_file_input_);
    editor_fields.push_back(ssh_config_host_input_);
    editor_fields.push_back(client_id_input_);
    editor_fields.push_back(client_secret_input_);
    editor_fields.push_back(tenant_id_input_);
    editor_fields.push_back(root_folder_id_input_);
    editor_fields.push_back(site_id_input_);
    editor_fields.push_back(drive_id_input_);
    editor_fields.push_back(app_key_input_);
    editor_fields.push_back(app_secret_input_);
    editor_fields.push_back(access_token_input_);
    editor_fields.push_back(refresh_token_input_);
    editor_fields.push_back(scope_input_);

    footer_actions_container_ = ftxui::Container::Horizontal({
        edit_button_,
        set_active_button_,
        test_button_,
        delete_button_,
        reload_button_,
        save_button_,
        save_token_button_,
        help_button_,
        authorize_button_,
        close_button_,
    });

    ftxui::Components main_children;
    main_children.push_back(ftxui::Container::Horizontal(tab_buttons));
    main_children.push_back(list_component_);
    main_children.push_back(ftxui::Container::Vertical(editor_fields));
    main_children.push_back(footer_actions_container_);
    auto main_container = ftxui::Container::Vertical(main_children);
    main_container = ftxui::CatchEvent(main_container, [this](ftxui::Event event) {
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowUp) {
            auto inputs = GetVisibleInputs();
            if (inputs.empty()) {
                return false;
            }
            int focused = FindFocusedInputIndex(inputs);
            if (event == ftxui::Event::ArrowDown) {
                int next = (focused + 1) % static_cast<int>(inputs.size());
                inputs[next]->TakeFocus();
            } else {
                int prev = (focused <= 0)
                    ? static_cast<int>(inputs.size()) - 1
                    : focused - 1;
                inputs[prev]->TakeFocus();
            }
            return true;
        }
        return false;
    });

    help_close_button_ = MakeTextButton("Close", [this] { CloseHelp(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges);
    copy_url_button_ = MakeTextButton("Copy URL", [this] { CopyHelpUrl(); },
        ButtonRole::Utility, ButtonVariant::AccentEdges, "⧉");
    ftxui::Components help_children;
    help_children.push_back(help_close_button_);
    help_container_ = ftxui::Container::Horizontal(help_children);

    ftxui::Components authorize_children;
    authorize_children.push_back(redirect_url_input_);
    authorize_children.push_back(submit_redirect_button_);
    authorize_children.push_back(cancel_authorize_button_);
    authorize_container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal(authorize_children),
    });

    ftxui::Components tab_children;
    tab_children.push_back(main_container);
    tab_children.push_back(help_container_);
    tab_children.push_back(authorize_container_);
    container_ = ftxui::Container::Tab(
        tab_children, &help_layer_index_);
    Reload();
}

ftxui::Component RemoteConnectionsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click,
    ButtonRole role,
    ButtonVariant variant,
    std::string icon,
    ButtonSize size) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.role = role;
    spec.variant = variant;
    spec.size = size;
    spec.icon = std::move(icon);
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

ftxui::Component RemoteConnectionsModalContent::MakeTabButton(
    std::string label,
    MainTab tab) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = "  " + label + "  ";
    option.on_click = [this, tab] { SelectTab(tab); };
    option.transform = [this, label = std::move(label), tab](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalTabButton(
            theme,
            label,
            selected_tab_ == tab,
            state.focused || state.active);
    };
    return ftxui::Button(option);
}

void RemoteConnectionsModalContent::Open() {
    selected_tab_ = MainTab::Connections;
    Reload();
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
    if (list_component_) {
        list_component_->TakeFocus();
    }
}

void RemoteConnectionsModalContent::Close() {
    output_.clear();
    status_ = "Ready.";
    status_is_error_ = false;
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
}

void RemoteConnectionsModalContent::Reload() {
    std::string error;
    if (config_store_ && !config_store_->Load(error)) {
        SetStatus(error, true);
    }
    connections_ = config_store_ ? config_store_->Connections() : std::vector<RemoteConnectionConfig>{};
    connection_boxes_.resize(connections_.size());
    if (connections_.empty()) {
        selected_connection_ = 0;
        ResetFormForType(RemoteConnectionType::Sftp);
        SetStatus("No remote connections. Open a provider tab and create one.");
        return;
    }
    const std::string active_id = config_store_ ? config_store_->ActiveConnectionId() : std::string{};
    if (!active_id.empty()) {
        for (size_t index = 0; index < connections_.size(); ++index) {
            if (connections_[index].id == active_id) {
                selected_connection_ = static_cast<int>(index);
                break;
            }
        }
    }
    selected_connection_ = std::clamp(selected_connection_, 0, static_cast<int>(connections_.size()) - 1);
    if (!id_value_.empty()) {
        for (size_t index = 0; index < connections_.size(); ++index) {
            if (connections_[index].id == id_value_) {
                selected_connection_ = static_cast<int>(index);
                LoadSelectedIntoForm();
                break;
            }
        }
    }
    SetStatus("Loaded " + std::to_string(connections_.size()) + " connection(s). Active: " + ActiveConnectionLabel());
}
