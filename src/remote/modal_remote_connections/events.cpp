bool RemoteConnectionsModalContent::HandleListEvent(ftxui::Event event) {
    if (event == ftxui::Event::ArrowDown) {
        SelectConnection(selected_connection_ + 1);
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        SelectConnection(selected_connection_ - 1);
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
        const int index = EntryIndexAtMouse(event.mouse());
        if (index >= 0) {
            SelectConnection(index);
            return true;
        }
    }
    return false;
}

int RemoteConnectionsModalContent::EntryIndexAtMouse(const ftxui::Mouse& mouse) const {
    for (size_t index = 0; index < connection_boxes_.size(); ++index) {
        if (connection_boxes_[index].Contain(mouse.x, mouse.y)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool RemoteConnectionsModalContent::HandleHelpEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        CloseHelp();
        return true;
    }
    return help_container_ ? help_container_->OnEvent(event) : true;
}

bool RemoteConnectionsModalContent::HandleEvent(ftxui::Event event) {
    if (help_active_) {
        return HandleHelpEvent(std::move(event));
    }

    if (authorize_pending_) {
        if (event == ftxui::Event::Escape) {
            authorize_pending_ = false;
            authorize_layer_index_ = 0;
            SetStatus("Authorization cancelled.");
            return true;
        }
        return authorize_container_ ? authorize_container_->OnEvent(event) : false;
    }

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
}

std::vector<ftxui::Component> RemoteConnectionsModalContent::GetVisibleInputs() {
    std::vector<ftxui::Component> inputs;
    if (selected_tab_ == MainTab::Connections) {
        return inputs;
    }
    const RemoteConnectionType visible_type = TypeForTab(selected_tab_);
    inputs.push_back(name_input_);

    switch (visible_type) {
        case RemoteConnectionType::Sftp:
            inputs.push_back(host_input_);
            inputs.push_back(port_input_);
            inputs.push_back(user_input_);
            inputs.push_back(password_input_);
            inputs.push_back(remote_root_input_);
            inputs.push_back(auth_mode_input_);
            inputs.push_back(identity_file_input_);
            inputs.push_back(key_passphrase_input_);
            inputs.push_back(known_hosts_file_input_);
            inputs.push_back(ssh_config_host_input_);
            break;
        case RemoteConnectionType::GoogleDrive:
            inputs.push_back(client_id_input_);
            inputs.push_back(client_secret_input_);
            inputs.push_back(root_folder_id_input_);
            inputs.push_back(scope_input_);
            inputs.push_back(access_token_input_);
            inputs.push_back(refresh_token_input_);
            break;
        case RemoteConnectionType::MicrosoftDrive:
            inputs.push_back(tenant_id_input_);
            inputs.push_back(client_id_input_);
            inputs.push_back(client_secret_input_);
            inputs.push_back(site_id_input_);
            inputs.push_back(drive_id_input_);
            inputs.push_back(remote_root_input_);
            inputs.push_back(scope_input_);
            inputs.push_back(access_token_input_);
            inputs.push_back(refresh_token_input_);
            break;
        case RemoteConnectionType::Dropbox:
            inputs.push_back(app_key_input_);
            inputs.push_back(app_secret_input_);
            inputs.push_back(remote_root_input_);
            inputs.push_back(access_token_input_);
            inputs.push_back(refresh_token_input_);
            break;
    }
    return inputs;
}

int RemoteConnectionsModalContent::FindFocusedInputIndex(
    const std::vector<ftxui::Component>& inputs) {
    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
        if (inputs[i] && inputs[i]->Focused()) {
            return i;
        }
    }
    return -1;
}
