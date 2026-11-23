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

    MakeTextButton(prev_connection_button_, "Prev Conn", [this] { PreviousConnection(); },
        ButtonRole::Navigation, ButtonVariant::AccentEdges, "‹");
    MakeTextButton(next_connection_button_, "Next Conn", [this] { NextConnection(); },
        ButtonRole::Navigation, ButtonVariant::AccentEdges, "›");
    MakeTextButton(refresh_button_, "Refresh", [this] { RefreshAll(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "⟳");
    MakeTextButton(connect_button_, "Connect", [this] { ConnectRemote(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    MakeTextButton(disconnect_button_, "Disconnect", [this] { DisconnectRemote(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges);
    MakeTextButton(copy_to_remote_button_, "Copy", [this] { CopyToRemote(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "→");
    MakeTextButton(copy_to_local_button_, "Copy", [this] { CopyToLocal(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "←");
    MakeTextButton(open_button_, "Open", [this] { OpenSelectedFile(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    MakeTextButton(sync_opened_button_, "Sync Last", [this] { UploadLastOpenedRemoteFile(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "↑");
    MakeTextButton(clear_cache_button_, "Clear Cache", [this] { ClearCachedRemoteFiles(); },
        ButtonRole::Danger, ButtonVariant::AccentEdges, "!");
    MakeTextButton(copy_path_button_, "Copy Path", [this] { CopySelectedPath(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges, "⧉");
    MakeTextButton(mkdir_button_, "Mkdir", [this] { StartMakeDirectory(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges, "+");
    MakeTextButton(rename_button_, "Rename", [this] { StartRename(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges);
    MakeTextButton(delete_button_, "Delete", [this] { StartDelete(); },
        ButtonRole::Danger, ButtonVariant::AccentEdges, "!");
    MakeTextButton(close_button_, "Close", [this] {
        if (on_close_) {
            on_close_();
        }
    }, ButtonRole::Cancel, ButtonVariant::AccentEdges);
    MakeTextButton(copy_error_button_, "Copy Error", [this] {
        if (error_footer_.empty()) {
            SetStatus("No error to copy.");
            return;
        }
        const bool written = copy_text_ && copy_text_(error_footer_);
        SetStatus(written ? "Error copied to clipboard." : ClipboardController::kWriteFailed, !written);
    }, ButtonRole::Cancel, ButtonVariant::AccentEdges, "⧉");

    local_path_input_ = MakePathInput(PanelSide::Local);
    remote_path_input_ = MakePathInput(PanelSide::Remote);

    local_list_component_ = ftxui::Make<PanelComponent>(*this, PanelSide::Local,
        ftxui::Menu(&local_panel_.entry_labels, &local_panel_.selected));
    remote_list_component_ = ftxui::Make<PanelComponent>(*this, PanelSide::Remote,
        ftxui::Menu(&remote_panel_.entry_labels, &remote_panel_.selected));

    ftxui::InputOption operation_option;
    operation_option.multiline = false;
    operation_option.cursor_position = &pending_input_cursor_;
    operation_option.on_enter = [this] { ConfirmPendingOperation(); };
    operation_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RemoteDialogInputTransform(theme, std::move(state));
    };
    operation_input_ = ClipboardController::MakeInput(&pending_input_value_, "name", operation_option);
    MakeTextButton(confirm_button_, "Confirm", [this] { ConfirmPendingOperation(); },
        ButtonRole::Primary, ButtonVariant::AccentEdges);
    MakeTextButton(cancel_button_, "Cancel", [this] { CancelPendingOperation(); },
        ButtonRole::Cancel, ButtonVariant::AccentEdges);

    container_inner_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            refresh_button_,
            copy_error_button_,
            connect_button_,
            disconnect_button_,
        }),
        ftxui::Container::Horizontal({
            local_path_input_,
            remote_path_input_,
        }),
        ftxui::Container::Horizontal({
            local_list_component_,
            remote_list_component_,
        }),
        ftxui::Maybe(ftxui::Container::Horizontal({
            operation_input_,
            confirm_button_,
            cancel_button_,
        }), [this] { return pending_operation_ != PendingOperation::None; }),
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
    });
    container_ = ftxui::CatchEvent(
        container_inner_,
        [this](ftxui::Event event) { return HandleGlobalContentEvent(std::move(event)); });
}

void RemoteFilesModalContent::MakeTextButton(
    ftxui::Component& button,
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
    option.transform = [this, &button, spec = std::move(spec)](const ftxui::EntryState&) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return RenderModalFlatButton(theme, spec, button->Focused());
    };
    button = ftxui::Button(option);
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
    return ClipboardController::MakeInput(&panel.path_input, side == PanelSide::Local ? "local path" : "remote path", option);
}

void RemoteFilesModalContent::Open(
    std::string preferred_connection_id,
    bool connect_immediately) {
    CancelPendingOperation();
    preferred_connection_id_ = std::move(preferred_connection_id);
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
    if (!connections_.empty() && !local_panel_.status_is_error) {
        SetStatus("Remote is disconnected. Press Connect to open the active connection.");
    }
    if (connect_immediately && !connections_.empty()) {
        ConnectRemote();
    }
}

void RemoteFilesModalContent::Close() {
    CancelPendingOperation();
    local_panel_.scrollbar_drag.reset();
    remote_panel_.scrollbar_drag.reset();
    local_panel_.entries.clear();
    remote_panel_.entries.clear();
    local_panel_.entry_labels.clear();
    remote_panel_.entry_labels.clear();
    remote_provider_.reset();
    SetStatus("Ready.");
}
