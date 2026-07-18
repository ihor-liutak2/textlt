void RemoteFilesModalContent::ReloadConnections() {
    std::string error;
    if (config_store_ && !config_store_->Load(error)) {
        SetStatus(error, true);
    }

    connections_.clear();
    const RemoteConnectionConfig* active = config_store_ ? config_store_->FindActiveConnection() : nullptr;
    if (active) {
        connections_.push_back(*active);
    }

    if (connections_.empty()) {
        selected_connection_ = 0;
        remote_provider_.reset();
        remote_panel_.entries.clear();
        remote_panel_.path = "/";
        remote_panel_.path_input = "/";
        SetPanelStatus(PanelSide::Remote,
            "No active remote connection. Open Remote Connections and set one active.", true);
        return;
    }
    selected_connection_ = 0;
    remote_provider_.reset();
    remote_panel_.entries.clear();
    remote_panel_.boxes.clear();
    remote_panel_.selected = 0;
    remote_panel_.path = connections_.front().remote_root.empty()
        ? "/"
        : connections_.front().remote_root;
    remote_panel_.path_input = remote_panel_.path;
    remote_panel_.path_cursor = static_cast<int>(remote_panel_.path_input.size());
    SetPanelStatus(PanelSide::Remote, "Disconnected. Press Connect.");
}

bool RemoteFilesModalContent::EnsureRemoteProvider() {
    if (connections_.empty()) {
        remote_provider_.reset();
        SetPanelStatus(PanelSide::Remote, "No remote connection configured.", true);
        return false;
    }

    if (!remote_provider_) {
        SetPanelStatus(PanelSide::Remote, "Disconnected. Press Connect.", true);
        SetStatus("Remote is disconnected. Press Connect.", true);
        return false;
    }
    return true;
}

void RemoteFilesModalContent::ConnectRemote() {
    error_footer_.clear();
    if (connections_.empty()) {
        remote_provider_.reset();
        SetPanelStatus(PanelSide::Remote, "No remote connection configured.", true);
        SetStatus("No active remote connection to connect.", true);
        return;
    }
    if (remote_provider_) {
        SetStatus("Remote connection is already connected.");
        return;
    }

    const RemoteConnectionConfig& config = connections_[static_cast<size_t>(selected_connection_)];
    std::unique_ptr<IRemoteProvider> provider;
    switch (config.type) {
        case RemoteConnectionType::Sftp:
            provider = std::make_unique<RemoteSftpProvider>();
            break;
        case RemoteConnectionType::Ftps:
            provider = std::make_unique<RemoteFtpsProvider>();
            break;
        case RemoteConnectionType::Dropbox:
            provider = std::make_unique<RemoteDropboxProvider>();
            break;
        case RemoteConnectionType::GoogleDrive:
            provider = std::make_unique<RemoteGoogleDriveProvider>();
            break;
        case RemoteConnectionType::MicrosoftDrive:
            provider = std::make_unique<RemoteMicrosoftDriveProvider>();
            break;
    }

    std::string error;
    if (!provider || !provider->Connect(config, error)) {
        remote_provider_.reset();
        SetPanelStatus(PanelSide::Remote, error.empty() ? "Cannot connect remote provider." : error, true);
        SetStatus(error.empty() ? "Cannot connect remote provider." : error, true);
        return;
    }
    remote_provider_ = std::move(provider);
    SetPanelStatus(PanelSide::Remote, "Connected active: " + CurrentConnectionLabel(connections_, selected_connection_));
    SetStatus("Connected: " + CurrentConnectionLabel(connections_, selected_connection_));
    LoadPanel(PanelSide::Remote, config.remote_root.empty() ? "/" : config.remote_root);
}

void RemoteFilesModalContent::DisconnectRemote() {
    error_footer_.clear();
    CancelPendingOperation();
    if (!remote_provider_) {
        SetStatus("Remote connection is already disconnected.");
        SetPanelStatus(PanelSide::Remote, "Disconnected. Press Connect.");
        return;
    }
    remote_provider_.reset();
    remote_panel_.entries.clear();
    remote_panel_.boxes.clear();
    remote_panel_.selected = 0;
    SetPanelStatus(PanelSide::Remote, "Disconnected. Press Connect.");
    SetStatus("Remote connection disconnected.");
}

void RemoteFilesModalContent::LoadPanel(PanelSide side, const std::string& path) {
    PanelState& panel = Panel(side);
    IRemoteProvider* provider = Provider(side);
    if (!provider) {
        SetPanelStatus(side, "Provider is not available.", true);
        return;
    }

    std::vector<RemoteEntry> listed;
    std::string error;
    if (!provider->List(path, listed, error)) {
        SetPanelStatus(side, error.empty() ? "Cannot list directory." : error, true);
        return;
    }

    panel.path = side == PanelSide::Local ? path : NormalizeRemoteDirectory(path);
    panel.path_input = panel.path;
    panel.path_cursor = static_cast<int>(panel.path_input.size());
    panel.entries.clear();
    panel.boxes.clear();

    const std::string parent = side == PanelSide::Local
        ? FileManager::PathToUtf8(FileManager::PathFromUtf8(panel.path).parent_path())
        : RemoteParentPath(panel.path);
    if (!parent.empty() && parent != panel.path) {
        RemoteEntry parent_entry;
        parent_entry.name = "..";
        parent_entry.path = parent;
        parent_entry.type = RemoteEntryType::Directory;
        panel.entries.push_back(parent_entry);
    }

    panel.entries.insert(panel.entries.end(), listed.begin(), listed.end());
    panel.boxes.resize(panel.entries.size());
    panel.selected = panel.entries.empty()
        ? 0
        : std::clamp(panel.selected, 0, static_cast<int>(panel.entries.size()) - 1);
    panel.scroll_offset = 0;
    SetPanelStatus(side, panel.entries.empty() ? "Directory is empty." : "Ready.");
}

void RemoteFilesModalContent::LoadLocalPathFromInput() {
    LoadPanel(PanelSide::Local, local_panel_.path_input);
}

void RemoteFilesModalContent::LoadRemotePathFromInput() {
    if (EnsureRemoteProvider()) {
        LoadPanel(PanelSide::Remote, remote_panel_.path_input);
    }
}

void RemoteFilesModalContent::RefreshAll() {
    error_footer_.clear();
    LoadPanel(PanelSide::Local, local_panel_.path);
    if (remote_provider_) {
        LoadPanel(PanelSide::Remote, remote_panel_.path);
        SetStatus("Refreshed panels.");
    } else {
        SetPanelStatus(PanelSide::Remote, "Disconnected. Press Connect.");
        SetStatus("Refreshed local panel. Remote is disconnected.");
    }
}

void RemoteFilesModalContent::RefreshActive() {
    if (active_panel_ == PanelSide::Remote && !EnsureRemoteProvider()) {
        return;
    }
    LoadPanel(active_panel_, Panel(active_panel_).path);
}

void RemoteFilesModalContent::SelectPanel(PanelSide side) {
    active_panel_ = side;
    if (side == PanelSide::Local && local_list_component_) {
        local_list_component_->TakeFocus();
    } else if (side == PanelSide::Remote && remote_list_component_) {
        remote_list_component_->TakeFocus();
    }
}

void RemoteFilesModalContent::SelectEntry(PanelSide side, int index) {
    PanelState& panel = Panel(side);
    if (panel.entries.empty()) {
        panel.selected = 0;
        return;
    }
    panel.selected = std::clamp(index, 0, static_cast<int>(panel.entries.size()) - 1);
    if (panel.selected < panel.scroll_offset) {
        panel.scroll_offset = panel.selected;
    } else if (panel.selected >= panel.scroll_offset + kVisibleRows) {
        panel.scroll_offset = panel.selected - kVisibleRows + 1;
    }
}

void RemoteFilesModalContent::OpenSelected(PanelSide side) {
    const RemoteEntry* entry = SelectedEntry(side);
    if (!entry) {
        SetStatus("No entry selected.", true);
        return;
    }
    if (entry->type == RemoteEntryType::Directory) {
        LoadPanel(side, entry->path);
        return;
    }
    if (side == PanelSide::Local) {
        OpenSelectedFile();
        return;
    }
    OpenSelectedFile();
}

void RemoteFilesModalContent::GoParent(PanelSide side) {
    PanelState& panel = Panel(side);
    const std::string parent = side == PanelSide::Local
        ? FileManager::PathToUtf8(FileManager::PathFromUtf8(panel.path).parent_path())
        : RemoteParentPath(panel.path);
    if (!parent.empty() && parent != panel.path) {
        LoadPanel(side, parent);
    }
}

void RemoteFilesModalContent::NextConnection() {
    SetStatus("Remote Files uses only the active connection. Change it in Remote Connections.");
}

void RemoteFilesModalContent::PreviousConnection() {
    SetStatus("Remote Files uses only the active connection. Change it in Remote Connections.");
}
