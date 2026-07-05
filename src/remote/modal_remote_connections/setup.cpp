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

    ftxui::InputOption root_option = base_option;
    root_option.cursor_position = &remote_root_cursor_;
    remote_root_input_ = ftxui::Input(&remote_root_value_, "/remote/path", root_option);

    ftxui::InputOption identity_option = base_option;
    identity_option.cursor_position = &identity_file_cursor_;
    identity_file_input_ = ftxui::Input(&identity_file_value_, "~/.ssh/id_ed25519", identity_option);

    ftxui::InputOption ssh_alias_option = base_option;
    ssh_alias_option.cursor_position = &ssh_config_host_cursor_;
    ssh_config_host_input_ = ftxui::Input(&ssh_config_host_value_, "Host alias from ~/.ssh/config", ssh_alias_option);

    ftxui::InputOption account_option = base_option;
    account_option.cursor_position = &account_label_cursor_;
    account_label_input_ = ftxui::Input(&account_label_value_, "account label / email", account_option);

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

    add_button_ = MakeTextButton("Add", [this] { AddConnection(); },
        ButtonRole::Primary, ButtonVariant::AccentBar, "+");
    delete_button_ = MakeTextButton("Delete", [this] { DeleteSelected(); },
        ButtonRole::Danger, ButtonVariant::AccentBar, "!");
    save_button_ = MakeTextButton("Save", [this] { SaveFormToSelected(); },
        ButtonRole::Primary, ButtonVariant::AccentBar);
    test_button_ = MakeTextButton("Test", [this] { TestSelected(); },
        ButtonRole::Secondary, ButtonVariant::AccentBar, "▶");
    token_button_ = MakeTextButton("Token", [this] { PrepareTokenFile(); },
        ButtonRole::Utility, ButtonVariant::AccentBar);
    sftp_type_button_ = MakeTextButton("SFTP", [this] { SelectType(RemoteConnectionType::Sftp); },
        ButtonRole::Toggle, ButtonVariant::Minimal);
    google_type_button_ = MakeTextButton("Google", [this] { SelectType(RemoteConnectionType::GoogleDrive); },
        ButtonRole::Toggle, ButtonVariant::Minimal);
    microsoft_type_button_ = MakeTextButton("Microsoft", [this] { SelectType(RemoteConnectionType::MicrosoftDrive); },
        ButtonRole::Toggle, ButtonVariant::Minimal);
    dropbox_type_button_ = MakeTextButton("Dropbox", [this] { SelectType(RemoteConnectionType::Dropbox); },
        ButtonRole::Toggle, ButtonVariant::Minimal);
    reload_button_ = MakeTextButton("Reload", [this] { Reload(); },
        ButtonRole::Utility, ButtonVariant::AccentBar, "⟳");
    help_button_ = MakeTextButton("Help Connect", [this] { OpenHelp(); },
        ButtonRole::Utility, ButtonVariant::AccentBar, "?");
    authorize_button_ = MakeTextButton("Authorize", [this] { AuthorizeConnection(); },
        ButtonRole::Primary, ButtonVariant::AccentBar);

    auto redirect_option = base_option;
    redirect_option.cursor_position = &redirect_url_cursor_;
    redirect_url_input_ = ftxui::Input(&redirect_url_value_, "paste redirect URL here", redirect_option);
    submit_redirect_button_ = MakeTextButton("Submit", [this] { SubmitRedirectUrl(); },
        ButtonRole::Primary, ButtonVariant::AccentBar);
    cancel_authorize_button_ = MakeTextButton("Cancel", [this] {
        authorize_pending_ = false;
        authorize_layer_index_ = 0;
        SetStatus("Authorization cancelled.");
    }, ButtonRole::Cancel, ButtonVariant::AccentBar);
    close_button_ = MakeTextButton("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    }, ButtonRole::Cancel, ButtonVariant::AccentBar);

    list_component_ = ftxui::CatchEvent(
        ftxui::Renderer([this] { return RenderConnectionList(); }),
        [this](ftxui::Event event) { return HandleListEvent(std::move(event)); });

    ftxui::Components toolbar_buttons;
    toolbar_buttons.push_back(add_button_);
    toolbar_buttons.push_back(delete_button_);
    toolbar_buttons.push_back(save_button_);
    toolbar_buttons.push_back(test_button_);
    toolbar_buttons.push_back(token_button_);
    toolbar_buttons.push_back(reload_button_);
    toolbar_buttons.push_back(close_button_);

    ftxui::Components type_buttons;
    type_buttons.push_back(sftp_type_button_);
    type_buttons.push_back(google_type_button_);
    type_buttons.push_back(microsoft_type_button_);
    type_buttons.push_back(dropbox_type_button_);

    ftxui::Components form_fields;
    form_fields.push_back(name_input_);
    form_fields.push_back(host_input_);
    form_fields.push_back(port_input_);
    form_fields.push_back(user_input_);
    form_fields.push_back(remote_root_input_);
    form_fields.push_back(identity_file_input_);
    form_fields.push_back(ssh_config_host_input_);
    form_fields.push_back(account_label_input_);
    form_fields.push_back(client_id_input_);
    form_fields.push_back(client_secret_input_);
    form_fields.push_back(tenant_id_input_);
    form_fields.push_back(root_folder_id_input_);
    form_fields.push_back(site_id_input_);
    form_fields.push_back(drive_id_input_);
    form_fields.push_back(app_key_input_);
    form_fields.push_back(app_secret_input_);
    form_fields.push_back(help_button_);
    form_fields.push_back(authorize_button_);

    ftxui::Components form_area;
    form_area.push_back(list_component_);
    form_area.push_back(ftxui::Container::Vertical(form_fields));

    ftxui::Components main_children;
    main_children.push_back(ftxui::Container::Horizontal(toolbar_buttons));
    main_children.push_back(ftxui::Container::Horizontal(type_buttons));
    main_children.push_back(ftxui::Container::Horizontal(form_area));
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
        ButtonRole::Cancel, ButtonVariant::AccentBar);
    copy_url_button_ = MakeTextButton("Copy URL", [this] { CopyHelpUrl(); },
        ButtonRole::Utility, ButtonVariant::AccentBar, "⧉");
    ftxui::Components help_children;
    help_children.push_back(copy_url_button_);
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
    return MakeButton(theme_, std::move(spec), std::move(on_click));
}

void RemoteConnectionsModalContent::Open() {
    Reload();
    help_active_ = false;
    authorize_pending_ = false;
    help_layer_index_ = 0;
    authorize_layer_index_ = 0;
    if (name_input_) {
        name_input_->TakeFocus();
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
        id_value_.clear();
        name_value_.clear();
        type_value_ = "sftp";
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
        SetStatus("No remote connections. Choose a type and press Add.");
        return;
    }
    selected_connection_ = std::clamp(selected_connection_, 0, static_cast<int>(connections_.size()) - 1);
    LoadSelectedIntoForm();
    SetStatus("Loaded " + std::to_string(connections_.size()) + " connection(s).");
}
