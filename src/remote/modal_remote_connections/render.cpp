ftxui::Element RemoteConnectionsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Element body = vbox({
        assignment_project_id_.empty()
            ? emptyElement()
            : text(" Assign connection to project: " + assignment_project_name_) |
                bold | color(theme.modal_accent),
        RenderTabButtons(),
        separator() | color(theme.modal_border),
        RenderCurrentTab() | flex,
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);

    if (help_active_ || overlay_layer_index_ == CopyLayer) {
        auto overlay = help_active_ ? RenderHelpOverlay() : RenderCopyOverlay();
        ftxui::Element centered = ftxui::vbox(
            ftxui::filler(),
            ftxui::hbox(filler(), overlay, filler()),
            ftxui::filler());
        ftxui::Elements layers;
        layers.push_back(body);
        layers.push_back(centered);
        return dbox(std::move(layers));
    }

    return body;
}

ftxui::Element RemoteConnectionsModalContent::RenderCopyOverlay() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        text(" Copy remote connection ") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        paragraph("Including secrets puts saved credentials in the clipboard JSON."),
        text(" "),
        include_secrets_checkbox_->Render(),
        text(" "),
        hbox({filler(), confirm_copy_button_->Render(), text(" "), cancel_copy_button_->Render()}),
    }) | size(WIDTH, LESS_THAN, 54) | borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background) | color(theme.modal_text_color) | clear_under;
}

ftxui::Element RemoteConnectionsModalContent::RenderCurrentTab() {
    switch (selected_tab_) {
        case MainTab::Connections:
            return RenderConnectionsTab();
        case MainTab::Sftp:
            return RenderSftpTab();
        case MainTab::Ftps:
            return RenderFtpsTab();
        case MainTab::Dropbox:
            return RenderDropboxTab();
    }
    return RenderConnectionsTab();
}

ftxui::Element RemoteConnectionsModalContent::RenderTabButtons() {
    using namespace ftxui;
    return hbox({
        connections_tab_button_->Render(), text(" "),
        sftp_tab_button_->Render(), text(" "),
        ftps_tab_button_->Render(), text(" "),
        dropbox_tab_button_->Render(),
        filler(),
    });
}

ftxui::Element RemoteConnectionsModalContent::RenderFieldGrid(
    const std::vector<std::pair<std::string, ftxui::Component>>& fields) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    for (size_t index = 0; index < fields.size(); index += 2) {
        Element left = RenderRemoteDialogInputFrame(theme, fields[index].first, fields[index].second) |
            size(WIDTH, EQUAL, 51);
        Element right = text("") | size(WIDTH, EQUAL, 51);
        if (index + 1 < fields.size()) {
            right = RenderRemoteDialogInputFrame(theme, fields[index + 1].first, fields[index + 1].second) |
                size(WIDTH, EQUAL, 51);
        }
        rows.push_back(hbox({
            left,
            text("  "),
            right,
        }));
    }
    return vbox(std::move(rows));
}

ftxui::Element RemoteConnectionsModalContent::RenderOutput(int max_lines) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    std::string message = TrimForDisplay(ActionMessageText(), 160);
    std::replace(message.begin(), message.end(), '\n', ' ');
    std::replace(message.begin(), message.end(), '\r', ' ');
    std::replace(message.begin(), message.end(), '\t', ' ');

    return paragraph(message) |
        (status_is_error_ ? color(ftxui::Color::Red) : color(theme.modal_text_color)) |
        size(HEIGHT, EQUAL, std::max(2, max_lines + 1)) |
        bgcolor(theme.modal_input_bg) |
        borderStyled(LIGHT, theme.modal_border);
}

ftxui::Element RemoteConnectionsModalContent::RenderActionMessage() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    std::string message = status_;
    if (status_is_error_ && !output_.empty()) {
        const size_t line_end = output_.find('\n');
        message = output_.substr(0, line_end);
    }
    if (status_is_error_) {
        message = "Error: " + message;
    }

    return hbox({
        text(" " + TrimForDisplay(message, 70)),
        filler(),
    }) |
        size(HEIGHT, EQUAL, 1) |
        (status_is_error_ ? color(Color::Red) : color(theme.modal_text_color)) |
        bgcolor(theme.modal_input_bg);
}

int RemoteConnectionsModalContent::GetCustomFooterHeight() const {
    return selected_tab_ == MainTab::Connections &&
        ftxui::Terminal::Size().dimx < GetModalSizePreference().width ? 2 : 1;
}

ftxui::Element RemoteConnectionsModalContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements buttons;
    Element first_row;

    if (selected_tab_ == MainTab::Connections) {
        buttons.push_back(edit_button_->Render());
        buttons.push_back(text(" "));
        buttons.push_back(copy_connection_button_->Render());
        buttons.push_back(text(" "));
        buttons.push_back(paste_connection_button_->Render());
        buttons.push_back(text(" "));
        if (!assignment_project_id_.empty()) {
            buttons.push_back(assign_project_button_->Render());
        } else {
            buttons.push_back(set_active_button_->Render());
        }
        if (GetCustomFooterHeight() == 2) {
            buttons.push_back(filler());
            first_row = hbox(std::move(buttons));
            buttons.clear();
        } else {
            buttons.push_back(text(" "));
        }
        buttons.push_back(test_button_->Render());
        buttons.push_back(text(" "));
        buttons.push_back(delete_button_->Render());
        buttons.push_back(text(" "));
        buttons.push_back(reload_button_->Render());
        buttons.push_back(text(" "));
        buttons.push_back(copy_message_button_->Render());
    } else {
        const RemoteConnectionType type = TypeForTab(selected_tab_);
        if (selected_tab_ == MainTab::Sftp || selected_tab_ == MainTab::Ftps ||
            selected_tab_ == MainTab::Dropbox) {
            buttons.push_back(help_button_->Render());
            buttons.push_back(text(" "));
        }
        buttons.push_back(new_button_->Render());
        buttons.push_back(text(" "));
        buttons.push_back(save_button_->Render());
        if (type == RemoteConnectionType::Dropbox) {
            buttons.push_back(text(" "));
            buttons.push_back(save_token_button_->Render());
        }
        buttons.push_back(text(" "));
        buttons.push_back(test_button_->Render());
    }

    buttons.push_back(filler());
    buttons.push_back(close_button_->Render());

    auto last_row = hbox(std::move(buttons));
    auto footer = first_row ? vbox({std::move(first_row), std::move(last_row)}) : std::move(last_row);
    return footer |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color);
}
