RemoteFilesModalContent::RemoteFilesModalContent(
    const Theme* theme,
    RemoteConfigStore* config_store,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    OpenLocalFileCallback open_local_file,
    CopyTextCallback copy_text,
    CloseCallback on_close)
    : theme_(theme),
      config_store_(config_store),
      file_manager_(file_manager),
      start_directory_provider_(std::move(start_directory_provider)),
      open_local_file_(std::move(open_local_file)),
      copy_text_(std::move(copy_text)),
      on_close_(std::move(on_close)),
      local_provider_(file_manager) {
    local_panel_.title = "Local";
    remote_panel_.title = "Remote";

    prev_connection_button_ = MakeTextButton("Prev Conn", [this] { PreviousConnection(); },
        ButtonRole::Navigation, ButtonVariant::AccentEdges, "‹");
    next_connection_button_ = MakeTextButton("Next Conn", [this] { NextConnection(); },
        ButtonRole::Navigation, ButtonVariant::AccentEdges, "›");
    refresh_button_ = MakeTextButton("Refresh", [this] { RefreshAll(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "⟳");
    connect_button_ = MakeTextButton("Connect", [this] { ConnectRemote(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    disconnect_button_ = MakeTextButton("Disconnect", [this] { DisconnectRemote(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges);
    copy_to_remote_button_ = MakeTextButton("Copy", [this] { CopyToRemote(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "→");
    copy_to_local_button_ = MakeTextButton("Copy", [this] { CopyToLocal(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "←");
    open_button_ = MakeTextButton("Open", [this] { OpenSelectedFile(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    sync_opened_button_ = MakeTextButton("Sync Last", [this] { UploadLastOpenedRemoteFile(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "↑");
    clear_cache_button_ = MakeTextButton("Clear Cache", [this] { ClearCachedRemoteFiles(); },
        ButtonRole::Danger, ButtonVariant::AccentEdges, "!");
    copy_path_button_ = MakeTextButton("Copy Path", [this] { CopySelectedPath(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "⧉");
    mkdir_button_ = MakeTextButton("Mkdir", [this] { StartMakeDirectory(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges, "+");
    rename_button_ = MakeTextButton("Rename", [this] { StartRename(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges);
    delete_button_ = MakeTextButton("Delete", [this] { StartDelete(); },
        ButtonRole::Danger, ButtonVariant::AccentEdges, "!");
    close_button_ = MakeTextButton("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    }, ButtonRole::Cancel, ButtonVariant::AccentEdges);
    copy_error_button_ = MakeTextButton("Copy Error", [this] {
        if (error_footer_.empty()) {
            SetStatus("No error to copy.");
            return;
        }
        if (copy_text_) {
            copy_text_(error_footer_);
        }
        SetStatus("Error copied to clipboard.");
    }, ButtonRole::Cancel, ButtonVariant::AccentEdges, "⧉");

    local_path_input_ = MakePathInput(PanelSide::Local);
    remote_path_input_ = MakePathInput(PanelSide::Remote);

    auto local_menu = ftxui::Menu(
        &local_panel_.entry_labels,
        &local_panel_.selected);
    local_list_component_ = ftxui::CatchEvent(
        ftxui::Renderer(local_menu, [this] { return RenderPanel(PanelSide::Local); }),
        [this](ftxui::Event event) { return HandlePanelEvent(PanelSide::Local, std::move(event)); });
    auto remote_menu = ftxui::Menu(
        &remote_panel_.entry_labels,
        &remote_panel_.selected);
    remote_list_component_ = ftxui::CatchEvent(
        ftxui::Renderer(remote_menu, [this] { return RenderPanel(PanelSide::Remote); }),
        [this](ftxui::Event event) { return HandlePanelEvent(PanelSide::Remote, std::move(event)); });

    ftxui::InputOption operation_option;
    operation_option.multiline = false;
    operation_option.cursor_position = &pending_input_cursor_;
    operation_option.on_enter = [this] { ConfirmPendingOperation(); };
    operation_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RemoteDialogInputTransform(theme, std::move(state));
    };
    operation_input_ = ftxui::Input(&pending_input_value_, "name", operation_option);
    confirm_button_ = MakeTextButton("Confirm", [this] { ConfirmPendingOperation(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    cancel_button_ = MakeTextButton("Cancel", [this] { CancelPendingOperation(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges);

    container_inner_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            refresh_button_,
            copy_error_button_,
            connect_button_,
            disconnect_button_,
        }),
        ftxui::Container::Horizontal({
            copy_to_remote_button_,
            copy_to_local_button_,
            mkdir_button_,
            open_button_,
            sync_opened_button_,
            copy_path_button_,
            rename_button_,
            delete_button_,
            clear_cache_button_,
        }),
        ftxui::Container::Horizontal({
            local_path_input_,
            remote_path_input_,
        }),
        ftxui::Container::Horizontal({
            local_list_component_,
            remote_list_component_,
        }),
        ftxui::Container::Horizontal({
            operation_input_,
            confirm_button_,
            cancel_button_,
        }),
    });
    container_ = ftxui::CatchEvent(
        container_inner_,
        [this](ftxui::Event event) { return HandleGlobalContentEvent(std::move(event)); });
}

ftxui::Component RemoteFilesModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click,
    ButtonRole role,
    ButtonVariant variant,
    std::string icon,
    ButtonSize size) {
    ButtonSpec spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        variant,
        size,
        std::move(icon));
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalFlatButton(theme, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

ftxui::Component RemoteFilesModalContent::MakePathInput(PanelSide side) {
    PanelState& panel = Panel(side);
    ftxui::InputOption option;
    option.multiline = false;
    option.cursor_position = &panel.path_cursor;
    option.on_enter = [this, side] {
        if (side == PanelSide::Local) {
            LoadLocalPathFromInput();
        } else {
            LoadRemotePathFromInput();
        }
    };
    option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RemoteDialogInputTransform(theme, std::move(state));
    };
    return ftxui::Input(&panel.path_input, side == PanelSide::Local ? "local path" : "remote path", option);
}

void RemoteFilesModalContent::Open() {
    CancelPendingOperation();
    ReloadConnections();

    std::filesystem::path start_path;
    if (start_directory_provider_) {
        start_path = start_directory_provider_();
    }
    if (start_path.empty()) {
        start_path = FileManager::CurrentProcessDirectory();
    }

    RemoteConnectionConfig local_config;
    local_config.remote_root = FileManager::PathToUtf8(start_path);
    std::string error;
    if (!local_provider_.Connect(local_config, error)) {
        SetPanelStatus(PanelSide::Local, error, true);
    }
    LoadPanel(PanelSide::Local, FileManager::PathToUtf8(start_path));

    active_panel_ = PanelSide::Local;
    if (local_list_component_) {
        local_list_component_->TakeFocus();
    }
    SetStatus("Remote is disconnected. Press Connect to open the active connection.");
}

void RemoteFilesModalContent::Close() {
    CancelPendingOperation();
    local_panel_.entries.clear();
    remote_panel_.entries.clear();
    local_panel_.entry_labels.clear();
    remote_panel_.entry_labels.clear();
    remote_provider_.reset();
    SetStatus("Ready.");
}
