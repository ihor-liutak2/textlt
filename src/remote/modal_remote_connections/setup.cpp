RemoteConnectionsModalContent::RemoteConnectionsModalContent(
    const Theme* theme,
    RemoteConfigStore* config_store,
    WriteClipboardCallback write_clipboard,
    ReadClipboardCallback read_clipboard,
    AssignProjectCallback assign_project,
    DeleteGuardCallback can_delete,
    CloseCallback on_close)
    : theme_(theme),
      config_store_(config_store),
      write_clipboard_(std::move(write_clipboard)),
      read_clipboard_(std::move(read_clipboard)),
      assign_project_(std::move(assign_project)),
      can_delete_(std::move(can_delete)),
      on_close_(std::move(on_close)) {
    const auto make_input = [this](std::string& value, int& cursor, std::string placeholder,
                                   std::optional<MainTab> tab = std::nullopt,
                                   bool* password_masked = nullptr) {
        ftxui::InputOption option;
        option.multiline = false;
        if (password_masked) option.password = password_masked;
        option.transform = [this, password_masked](ftxui::InputState state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            if (password_masked && *password_masked && !state.is_placeholder) {
                using namespace ftxui;
                state.element = hbox({
                    text("●●●●●●●●"),
                    state.focused ? text(" ") | focusCursorBarBlinking : emptyElement(),
                });
            }
            return RemoteDialogInputTransform(theme, std::move(state));
        };
        auto selection = std::make_shared<InputSelection>();
        input_selections_.push_back(selection);
        return ftxui::Maybe(MakeSelectableInput(value, cursor, std::move(placeholder),
            std::move(option), std::move(selection)), [this, tab] {
                return selected_tab_ != MainTab::Connections && (!tab || selected_tab_ == *tab);
            });
    };

    name_input_ = make_input(name_value_, name_cursor_, "connection name");
    remote_root_input_ = make_input(remote_root_value_, remote_root_cursor_, "/remote/path");
    host_input_ = make_input(host_value_, host_cursor_, "host", MainTab::Ftps);
    port_input_ = make_input(port_value_, port_cursor_, "22", MainTab::Ftps);
    user_input_ = make_input(user_value_, user_cursor_, "user", MainTab::Ftps);
    password_input_ = make_input(
        password_value_, password_cursor_, "password", MainTab::Ftps, &password_masked_);
    ftxui::ButtonOption toggle_password_option = ftxui::ButtonOption::Simple();
    toggle_password_option.on_click = [this] { password_masked_ = !password_masked_; };
    toggle_password_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        const auto spec = ButtonSpecFromLabel(password_masked_ ? "Show" : "Hide",
            ButtonRole::Utility, ButtonVariant::Minimal, ButtonSize::Compact);
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    toggle_password_button_ = ftxui::Maybe(ftxui::Button(toggle_password_option),
        [this] { return selected_tab_ == MainTab::Ftps; });
    ftps_tls_mode_input_ = make_input(
        ftps_tls_mode_value_, ftps_tls_mode_cursor_, "explicit / implicit", MainTab::Ftps);
    auth_mode_input_ = make_input(auth_mode_value_, auth_mode_cursor_,
        "auto / password / private_key / agent / ssh_config", MainTab::Sftp);
    identity_file_input_ = make_input(
        identity_file_value_, identity_file_cursor_, "~/.ssh/id_ed25519", MainTab::Sftp);
    key_passphrase_input_ = make_input(
        key_passphrase_value_, key_passphrase_cursor_, "private key passphrase", MainTab::Sftp);
    known_hosts_file_input_ = make_input(
        known_hosts_file_value_, known_hosts_file_cursor_, "~/.ssh/known_hosts", MainTab::Sftp);

    ftxui::MenuOption ssh_host_option = ftxui::MenuOption::Vertical();
    ssh_host_option.on_change = [this] { SyncSshConfigHostSelection(); };
    ssh_host_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(" " + state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg) |
                ftxui::bold;
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    ssh_config_host_menu_ = ftxui::Maybe(ftxui::Menu(
        &ssh_config_hosts_, &selected_ssh_config_host_, ssh_host_option),
        [this] { return selected_tab_ == MainTab::Sftp && !ssh_config_hosts_.empty(); });

    app_key_input_ = make_input(
        app_key_value_, app_key_cursor_, "Dropbox app key", MainTab::Dropbox);
    app_secret_input_ = make_input(
        app_secret_value_, app_secret_cursor_, "Dropbox app secret", MainTab::Dropbox);
    access_token_input_ = make_input(
        access_token_value_, access_token_cursor_, "paste access token", MainTab::Dropbox);
    refresh_token_input_ = make_input(
        refresh_token_value_, refresh_token_cursor_, "paste refresh token", MainTab::Dropbox);

    ftxui::CheckboxOption checkbox_option = ftxui::CheckboxOption::Simple();
    checkbox_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(
            std::string(state.state ? "[X] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return item | ftxui::color(theme.modal_text_color);
    };
    ftps_passive_checkbox_ = ftxui::Maybe(ftxui::Checkbox(
        "Passive mode", &ftps_passive_value_, checkbox_option),
        [this] { return selected_tab_ == MainTab::Ftps; });

    delete_button_ = MakeTextButton("Delete", [this] { DeleteSelected(); },
        ButtonRole::Danger, ButtonVariant::AccentEdges, "!");
    new_button_ = MakeTextButton("New", [this] {
        AddConnectionOfType(TypeForTab(selected_tab_));
    }, ButtonRole::Utility, ButtonVariant::AccentEdges, "+");
    save_button_ = MakeTextButton("Save", [this] { SaveFormToSelected(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    test_button_ = MakeTextButton("Test", [this] { TestSelected(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "▶");
    set_active_button_ = MakeTextButton("Set Default", [this] { SetSelectedActive(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    assign_project_button_ = MakeTextButton("Set to Project", [this] { AssignSelectedToProject(); },
        ButtonRole::Success, ButtonVariant::AccentEdges);
    save_token_button_ = MakeTextButton("Save Token", [this] { SaveCloudAccessToken(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    edit_button_ = MakeTextButton("Edit", [this] { EditSelected(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    connections_tab_button_ = MakeTabButton("Connections", MainTab::Connections);
    sftp_tab_button_ = MakeTabButton("SFTP", MainTab::Sftp);
    ftps_tab_button_ = MakeTabButton("FTPS", MainTab::Ftps);
    dropbox_tab_button_ = MakeTabButton("Dropbox", MainTab::Dropbox);
    reload_button_ = MakeTextButton("Reload", [this] { Reload(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "⟳");
    copy_message_button_ = MakeTextButton("Copy Message", [this] { CopyActionMessage(); },
        ButtonRole::Utility, ButtonVariant::AccentEdges, "⧉");
    copy_connection_button_ = MakeTextButton("Copy", [this] { OpenCopyConnection(); }, ButtonRole::Utility);
    paste_connection_button_ = MakeTextButton("Paste", [this] { PasteConnection(); }, ButtonRole::Utility);
    help_button_ = MakeTextButton("Info", [this] { OpenHelp(); },
        ButtonRole::Utility, ButtonVariant::AccentEdges, "i");
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
    tab_buttons.push_back(ftps_tab_button_);
    tab_buttons.push_back(dropbox_tab_button_);

    ftxui::Components editor_fields;
    editor_fields.push_back(name_input_);
    editor_fields.push_back(host_input_);
    editor_fields.push_back(port_input_);
    editor_fields.push_back(user_input_);
    editor_fields.push_back(password_input_);
    editor_fields.push_back(toggle_password_button_);
    editor_fields.push_back(remote_root_input_);
    editor_fields.push_back(auth_mode_input_);
    editor_fields.push_back(identity_file_input_);
    editor_fields.push_back(key_passphrase_input_);
    editor_fields.push_back(known_hosts_file_input_);
    editor_fields.push_back(ssh_config_host_menu_);
    editor_fields.push_back(app_key_input_);
    editor_fields.push_back(app_secret_input_);
    editor_fields.push_back(access_token_input_);
    editor_fields.push_back(refresh_token_input_);
    editor_fields.push_back(ftps_tls_mode_input_);
    editor_fields.push_back(ftps_passive_checkbox_);

    footer_actions_container_ = ftxui::Container::Horizontal({});
    RebuildFooterActions();

    ftxui::Components main_children;
    main_children.push_back(ftxui::Container::Horizontal(tab_buttons));
    main_children.push_back(list_component_);
    main_children.push_back(ftxui::Container::Vertical(editor_fields));
    main_children.push_back(footer_actions_container_);
    auto main_container = ftxui::Container::Vertical(main_children);
    main_container = ftxui::CatchEvent(main_container, [this](ftxui::Event event) {
        if (selected_tab_ == MainTab::Sftp && ssh_config_host_menu_ &&
            ssh_config_host_menu_->Focused() &&
            (event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowUp)) {
            return false;
        }
        if (event == ftxui::Event::ArrowDown || event == ftxui::Event::ArrowUp ||
            event == ftxui::Event::Tab || event == ftxui::Event::TabReverse) {
            auto inputs = GetVisibleInputs();
            if (inputs.empty()) {
                return false;
            }
            int focused = FindFocusedInputIndex(inputs);
            if (focused < 0 && event != ftxui::Event::Tab && event != ftxui::Event::TabReverse) {
                return false;
            }
            if (event == ftxui::Event::ArrowDown || event == ftxui::Event::Tab) {
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

    include_secrets_checkbox_ = ftxui::Checkbox("Include secrets", &include_secrets_, checkbox_option);
    confirm_copy_button_ = MakeTextButton("Copy JSON", [this] { CopyConnection(); },
        ButtonRole::Primary);
    cancel_copy_button_ = MakeTextButton("Cancel", [this] { CloseCopyConnection(); },
        ButtonRole::Cancel);
    auto copy_container = ftxui::CatchEvent(ftxui::Container::Vertical({
        include_secrets_checkbox_,
        ftxui::Container::Horizontal({confirm_copy_button_, cancel_copy_button_}),
    }), [this](ftxui::Event event) {
        if (event != ftxui::Event::Escape) return false;
        CloseCopyConnection();
        return true;
    });

    ftxui::Components tab_children;
    tab_children.push_back(main_container);
    tab_children.push_back(help_container_);
    tab_children.push_back(copy_container);
    container_ = ftxui::Container::Tab(
        tab_children, &overlay_layer_index_);
    ReloadSshConfigHosts();
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
    assignment_project_id_.clear();
    assignment_project_name_.clear();
    preferred_connection_id_.clear();
    selected_tab_ = MainTab::Connections;
    RebuildFooterActions();
    ReloadSshConfigHosts();
    Reload();
    help_active_ = false;
    overlay_layer_index_ = MainLayer;
    if (list_component_) {
        list_component_->TakeFocus();
    }
}

void RemoteConnectionsModalContent::OpenForProject(
    std::string project_id,
    std::string project_name,
    std::string current_connection_id) {
    assignment_project_id_ = std::move(project_id);
    assignment_project_name_ = std::move(project_name);
    preferred_connection_id_ = std::move(current_connection_id);
    selected_connection_ = 0;
    id_value_.clear();
    selected_tab_ = MainTab::Connections;
    RebuildFooterActions();
    ReloadSshConfigHosts();
    Reload();
    help_active_ = false;
    overlay_layer_index_ = MainLayer;
    if (list_component_) {
        list_component_->TakeFocus();
    }
    SetStatus("Select a connection for project: " + assignment_project_name_);
}

void RemoteConnectionsModalContent::OpenForEdit(std::string connection_id) {
    assignment_project_id_.clear();
    assignment_project_name_.clear();
    preferred_connection_id_ = std::move(connection_id);
    id_value_.clear();
    selected_tab_ = MainTab::Connections;
    ReloadSshConfigHosts();
    Reload();
    EditSelected();
}

void RemoteConnectionsModalContent::Close() {
    ++paste_request_;
    include_secrets_ = false;
    output_.clear();
    status_ = "Ready.";
    status_is_error_ = false;
    help_active_ = false;
    overlay_layer_index_ = MainLayer;
    assignment_project_id_.clear();
    assignment_project_name_.clear();
    preferred_connection_id_.clear();
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
    const std::string selection_id = assignment_project_id_.empty() && preferred_connection_id_.empty()
        ? active_id : preferred_connection_id_;
    if (!selection_id.empty()) {
        for (size_t index = 0; index < connections_.size(); ++index) {
            if (connections_[index].id == selection_id) {
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
