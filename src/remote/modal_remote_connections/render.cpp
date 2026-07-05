ftxui::Element RemoteConnectionsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element body = vbox({
        hbox({
            add_button_->Render(), text(" "),
            save_button_->Render(), text(" "),
            delete_button_->Render(), text(" "),
            test_button_->Render(), text(" "),
            token_button_->Render(), text(" "),
            reload_button_->Render(), text(" "),
            close_button_->Render(),
        }),
        RenderTypeButtons(),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                text(" Connections ") | bold | color(theme.modal_accent),
                list_component_->Render() |
                    size(WIDTH, EQUAL, 37) |
                    size(HEIGHT, EQUAL, 14) |
                    border,
            }),
            separator(),
            RenderForm() | size(WIDTH, EQUAL, 70) | yframe | vscroll_indicator,
        }),
        separator() | color(theme.modal_border),
        RenderOutput(),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);

    if (help_active_) {
        ftxui::Element overlay = RenderHelpOverlay();
        ftxui::Element centered = ftxui::vbox(
            ftxui::filler(),
            ftxui::hbox(filler(), overlay, filler()),
            ftxui::filler());
        ftxui::Elements layers;
        layers.push_back(body);
        layers.push_back(centered);
        return dbox(std::move(layers));
    }

    if (authorize_pending_) {
        ftxui::Element auth_overlay = ftxui::vbox({
            ftxui::text(" Paste the redirect URL or access token ") |
                bold | color(theme.modal_accent),
            ftxui::separator() | color(theme.modal_border),
            ftxui::text(" The authorization URL was copied to your clipboard.") | color(theme.modal_text_color),
            ftxui::text(" Open it in your browser, authorize, then copy the") | color(theme.modal_text_color),
            ftxui::text(" redirect URL from the address bar and paste it below.") | color(theme.modal_text_color),
            ftxui::text(" Or paste a raw access token from the Dropbox app console.") | color(theme.modal_text_color),
            ftxui::text(""),
            redirect_url_input_->Render() |
                bgcolor(theme.modal_input_bg) |
                color(theme.modal_input_fg),
            ftxui::text(""),
            ftxui::hbox({
                ftxui::filler(),
                submit_redirect_button_->Render(),
                ftxui::text(" "),
                cancel_authorize_button_->Render(),
            }),
        }) |
            ftxui::size(WIDTH, LESS_THAN, 70) |
            ftxui::border |
            bgcolor(theme.modal_background) |
            color(theme.modal_border) |
            ftxui::clear_under;

        ftxui::Element centered = ftxui::vbox(
            ftxui::filler(),
            ftxui::hbox(filler(), auth_overlay, filler()),
            ftxui::filler());
        ftxui::Elements layers;
        layers.push_back(body);
        layers.push_back(centered);
        return dbox(std::move(layers));
    }

    return body;
}

ftxui::Element RemoteConnectionsModalContent::RenderConnectionList() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (connections_.empty()) {
        return vbox({
            filler(),
            text("No connections") | center | color(theme.modal_text_color),
            filler(),
        });
    }

    Elements rows;
    rows.reserve(connections_.size());
    for (size_t index = 0; index < connections_.size(); ++index) {
        const RemoteConnectionConfig& config = connections_[index];
        const bool selected = static_cast<int>(index) == selected_connection_;
        std::string label = config.name.empty() ? config.id : config.name;
        if (label.empty()) {
            label = "Unnamed";
        }
        const std::string target = ConnectionTargetSummary(config);
        Element row = hbox({
            text(" " + TrimForDisplay(label, 17)) | bold,
            filler(),
            text(RemoteConnectionTypeToString(config.type)) | dim,
        }) | reflect(connection_boxes_[index]);
        row = vbox({
            row,
            text("  " + TrimForDisplay(target, 30)) | dim,
        });
        if (selected) {
            row = row |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        } else {
            row = row | color(theme.modal_text_color);
        }
        rows.push_back(row);
    }
    return vbox(std::move(rows)) | yframe | vscroll_indicator;
}

ftxui::Element RemoteConnectionsModalContent::RenderTypeButtons() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    auto chip = [&](RemoteConnectionType type, const ftxui::Component& component) {
        Element rendered = component->Render();
        if (CurrentType() == type) {
            rendered = rendered |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        }
        return rendered;
    };

    return hbox({
        text(" Type: ") | color(theme.modal_accent),
        chip(RemoteConnectionType::Sftp, sftp_type_button_), text(" "),
        chip(RemoteConnectionType::GoogleDrive, google_type_button_), text(" "),
        chip(RemoteConnectionType::MicrosoftDrive, microsoft_type_button_), text(" "),
        chip(RemoteConnectionType::Dropbox, dropbox_type_button_),
        filler(),
        text(" Current: " + TypeDisplayName(CurrentType()) + " ") | dim | color(theme.modal_text_color),
    });
}

ftxui::Element RemoteConnectionsModalContent::RenderForm() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    auto field = [&](const std::string& label, ftxui::Component component) {
        return hbox({
            text(" " + label) | size(WIDTH, EQUAL, 18) | color(theme.modal_accent),
            component->Render() | flex,
        });
    };
    auto note = [&](const std::string& text_value) {
        return text(" " + text_value) | dim | color(theme.modal_text_color);
    };

    Elements rows;
    rows.push_back(text(" Connection settings ") | bold | color(theme.modal_accent));
    rows.push_back(field("Name", name_input_));

    switch (CurrentType()) {
        case RemoteConnectionType::Sftp:
            rows.push_back(field("Host", host_input_));
            rows.push_back(field("Port", port_input_));
            rows.push_back(field("User", user_input_));
            rows.push_back(field("Remote root", remote_root_input_));
            rows.push_back(field("Identity file", identity_file_input_));
            rows.push_back(field("SSH config", ssh_config_host_input_));
            rows.push_back(note("Passwords are not stored. Use SSH keys, ssh-agent or ~/.ssh/config."));
            rows.push_back(note("SFTP is the active file-manager backend."));
            break;
        case RemoteConnectionType::GoogleDrive:
            rows.push_back(field("Account label", account_label_input_));
            rows.push_back(field("Client ID", client_id_input_));
            rows.push_back(field("Client secret", client_secret_input_));
            rows.push_back(field("Root folder ID", root_folder_id_input_));
            rows.push_back(note("Press Authorize to open Google login in your browser."));
            rows.push_back(note("Leave Root folder ID empty to use the Drive root. Google Drive file operations are active now."));
            rows.push_back(ftxui::hbox({
                ftxui::filler(),
                authorize_button_->Render(),
            }));
            break;
        case RemoteConnectionType::MicrosoftDrive:
            rows.push_back(field("Account label", account_label_input_));
            rows.push_back(field("Tenant ID", tenant_id_input_));
            rows.push_back(field("Client ID", client_id_input_));
            rows.push_back(field("Client secret", client_secret_input_));
            rows.push_back(field("Site ID", site_id_input_));
            rows.push_back(field("Drive ID", drive_id_input_));
            rows.push_back(field("Remote root", remote_root_input_));
            rows.push_back(note("Use Tenant ID 'common' for personal accounts, or a tenant id for organization accounts."));
            rows.push_back(note("Press Authorize to open Microsoft login in your browser."));
            rows.push_back(ftxui::hbox({
                ftxui::filler(),
                authorize_button_->Render(),
            }));
            break;
        case RemoteConnectionType::Dropbox:
            rows.push_back(field("Account label", account_label_input_));
            rows.push_back(field("App key", app_key_input_));
            rows.push_back(field("App secret", app_secret_input_));
            rows.push_back(field("Remote root", remote_root_input_));
            rows.push_back(note("Press Authorize to open Dropbox login in your browser."));
            rows.push_back(note("Remote root can be / or /TextLT. Dropbox file operations are active now."));
            rows.push_back(ftxui::hbox({
                ftxui::filler(),
                authorize_button_->Render(),
                text(" "),
                help_button_->Render(),
            }));
            break;
    }

    return vbox(std::move(rows));
}

ftxui::Element RemoteConnectionsModalContent::RenderOutput() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    rows.push_back(
        text(" " + status_) |
        (status_is_error_ ? color(ftxui::Color::Red) : color(theme.modal_text_color)));

    if (!output_.empty()) {
        std::istringstream lines(output_);
        std::string line;
        int count = 0;
        while (count < 6 && std::getline(lines, line)) {
            rows.push_back(text(" " + TrimForDisplay(line, 104)) | dim | color(theme.modal_text_color));
            ++count;
        }
    }

    return vbox(std::move(rows)) |
        size(HEIGHT, EQUAL, 7) |
        yframe |
        vscroll_indicator |
        bgcolor(theme.modal_input_bg);
}
