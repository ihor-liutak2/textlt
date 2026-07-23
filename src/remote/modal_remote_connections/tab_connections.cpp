ftxui::Element RemoteConnectionsModalContent::RenderConnectionsTab() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        hbox({
            text(" Connections ") | bold | color(theme.modal_accent),
            filler(),
            text("Active: " + ActiveConnectionLabel()) | dim | color(theme.modal_text_color),
        }),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                text(" Saved profiles ") | bold | color(theme.modal_accent),
                list_component_->Render() |
                    size(WIDTH, EQUAL, 47) |
                    size(HEIGHT, EQUAL, 21) |
                    borderStyled(LIGHT, theme.modal_border),
            }),
            separator() | color(theme.modal_border),
            RenderConnectionDetails() | flex,
        }),
    }) |
        size(HEIGHT, EQUAL, 25) |
        bgcolor(theme.modal_background);
}

ftxui::Element RemoteConnectionsModalContent::RenderConnectionDetails() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements rows;
    rows.push_back(text(" Selected connection ") | bold | color(theme.modal_accent));
    rows.push_back(separator() | color(theme.modal_border));

    if (connections_.empty() || selected_connection_ < 0 ||
        selected_connection_ >= static_cast<int>(connections_.size())) {
        rows.push_back(text(" No connection selected.") | color(theme.modal_text_color));
        rows.push_back(text(" Open a type tab and press New to create one.") | dim | color(theme.modal_text_color));
        rows.push_back(text(" Connections has no input form by design.") | dim | color(theme.modal_text_color));
        rows.push_back(filler());
        rows.push_back(RenderOutput(4));
        return vbox(std::move(rows)) |
            size(HEIGHT, EQUAL, 23) |
            borderStyled(LIGHT, theme.modal_border) |
            bgcolor(theme.modal_input_bg);
    }

    const RemoteConnectionConfig& config = connections_[static_cast<size_t>(selected_connection_)];
    const std::string active_id = config_store_ ? config_store_->ActiveConnectionId() : std::string{};
    const bool active = !active_id.empty() && config.id == active_id;
    const bool notes_sync = config_store_ &&
        config.id == config_store_->NotesSyncConnectionId();
    rows.push_back(text(" Name: " + (config.name.empty() ? std::string("Unnamed") : config.name)) | color(theme.modal_text_color));
    rows.push_back(text(" Type: " + ConnectionKindLabel(config)) | color(theme.modal_text_color));
    rows.push_back(text(std::string(" Active: ") + (active ? "yes" : "no")) | color(theme.modal_text_color));
    rows.push_back(text(std::string(" Notes sync: ") + (notes_sync ? "yes" : "no")) |
        color(theme.modal_text_color));
    const std::string target = ConnectionTargetSummary(config);
    if (!target.empty()) {
        rows.push_back(text(" Target: " + target) | dim | color(theme.modal_text_color));
    }
    rows.push_back(text(""));
    rows.push_back(text(" Edit opens the matching type tab.") | dim | color(theme.modal_text_color));
    rows.push_back(text(" Remote Files uses only the active connection.") | dim | color(theme.modal_text_color));
    rows.push_back(text(" Notes Sync tests and assigns this profile; syncing remains manual.") |
        dim | color(theme.modal_text_color));
    rows.push_back(filler());
    rows.push_back(RenderOutput(4));

    return vbox(std::move(rows)) |
        size(HEIGHT, EQUAL, 23) |
        borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_input_bg);
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

    const std::string active_id = config_store_ ? config_store_->ActiveConnectionId() : std::string{};
    Elements rows;
    rows.reserve(connections_.size());
    for (size_t index = 0; index < connections_.size(); ++index) {
        const RemoteConnectionConfig& config = connections_[index];
        const bool selected = static_cast<int>(index) == selected_connection_;
        const bool active = !active_id.empty() && config.id == active_id;
        std::string label = config.name.empty() ? config.id : config.name;
        if (label.empty()) {
            label = "Unnamed";
        }
        const std::string target = ConnectionTargetSummary(config);
        Element row = hbox({
            text(active ? "* " : "  ") | bold | color(theme.modal_accent),
            text(TrimForDisplay(label, 19)) | bold,
            filler(),
            text(ConnectionKindLabel(config)) | dim,
        }) | reflect(connection_boxes_[index]);
        row = vbox({
            row,
            text("  " + TrimForDisplay(target, 33)) | dim,
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
