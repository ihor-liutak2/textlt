ftxui::Element RemoteFilesModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string connection_label = CurrentConnectionLabel(connections_, selected_connection_);

    return vbox({
        hbox({
            text(" Connection: ") | color(theme.modal_accent),
            text(TrimForDisplay(connection_label, 28)) | bold | color(theme.modal_text_color),
            filler(),
            prev_connection_button_->Render(), text(" "),
            next_connection_button_->Render(), text(" "),
            refresh_button_->Render(), text(" "),
            copy_error_button_->Render(),
        }),
        hbox({
            copy_to_remote_button_->Render(), text(" "),
            copy_to_local_button_->Render(), text(" "),
            mkdir_button_->Render(), text(" "),
            open_button_->Render(), text(" "),
            sync_opened_button_->Render(), text(" "),
            copy_path_button_->Render(), text(" "),
            rename_button_->Render(), text(" "),
            delete_button_->Render(), text(" "),
            clear_cache_button_->Render(),
        }),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                hbox({text(" Local path ") | color(theme.modal_accent), local_path_input_->Render() | flex}),
            }) | size(WIDTH, EQUAL, 58),
            separator(),
            vbox({
                hbox({text(" Remote path ") | color(theme.modal_accent), remote_path_input_->Render() | flex}),
            }) | size(WIDTH, EQUAL, 58),
        }),
        hbox({
            local_list_component_->Render() | size(WIDTH, EQUAL, 58) | border,
            remote_list_component_->Render() | size(WIDTH, EQUAL, 58) | border,
        }),
        RenderOperationRow(),
        hbox({
            text(" " + LastCachedLabel()) | dim | color(theme.modal_text_color),
            filler(),
        }),
        hbox({
            text(" " + status_) |
                (status_is_error_ ? color(ftxui::Color::Red) : color(theme.modal_text_color)),
            filler(),
            text(" Active: " + std::string(active_panel_ == PanelSide::Local ? "Local" : "Remote") + " ") |
                dim | color(theme.modal_text_color),
        }),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

ftxui::Element RemoteFilesModalContent::RenderPanel(PanelSide side) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const PanelState& panel = Panel(side);
    const bool active = side == active_panel_;

    Elements rows;
    rows.push_back(hbox({
        text(" " + panel.title + " ") | bold |
            (active ? bgcolor(theme.modal_selected_item_bg) : bgcolor(theme.modal_background)) |
            (active ? color(theme.modal_selected_item_fg) : color(theme.modal_accent)),
        filler(),
        text(panel.status.empty() ? " " : TrimForDisplay(panel.status, 36)) |
            dim |
            (panel.status_is_error ? color(ftxui::Color::Red) : color(theme.modal_text_color)),
    }));
    rows.push_back(separator() | color(theme.modal_border));

    if (panel.entries.empty()) {
        rows.push_back(filler());
        rows.push_back(text("No entries") | center | color(theme.modal_text_color));
        rows.push_back(filler());
        return vbox(std::move(rows)) |
            size(HEIGHT, EQUAL, kVisibleRows + 3) |
            bgcolor(theme.modal_input_bg);
    }

    const int start = std::max(0, panel.scroll_offset);
    const int end = std::min(static_cast<int>(panel.entries.size()), start + kVisibleRows);
    for (int index = start; index < end; ++index) {
        rows.push_back(RenderEntryRow(side, static_cast<size_t>(index)));
    }
    while (static_cast<int>(rows.size()) < kVisibleRows + 2) {
        rows.push_back(text(" "));
    }
    return vbox(std::move(rows)) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

ftxui::Element RemoteFilesModalContent::RenderEntryRow(PanelSide side, size_t index) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    PanelState& panel = Panel(side);
    const RemoteEntry& entry = panel.entries[index];
    const bool selected = static_cast<int>(index) == panel.selected;
    const std::string prefix = entry.type == RemoteEntryType::Directory ? "[D] " : "    ";

    Element row = hbox({
        text(" " + prefix + TrimForDisplay(entry.name, 36)) | bold,
        filler(),
        text(EntryTypeLabel(entry.type)) | dim | size(WIDTH, EQUAL, 6),
        text(EntrySizeLabel(entry)) | dim | size(WIDTH, EQUAL, 10),
    }) | reflect(panel.boxes[index]);

    if (selected) {
        row = row |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg);
    } else {
        row = row | color(theme.modal_text_color);
    }
    return row;
}

ftxui::Element RemoteFilesModalContent::RenderOperationRow() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (pending_operation_ == PendingOperation::None) {
        return hbox({
            text(" Enter opens. Backspace goes up. F5 copies file/folder. Existing targets require OVERWRITE confirmation. ") |
                dim | color(theme.modal_text_color),
            filler(),
        });
    }

    return hbox({
        text(" " + pending_input_label_ + " ") | color(theme.modal_accent),
        operation_input_->Render() | size(WIDTH, EQUAL, 42),
        text(" "),
        confirm_button_->Render(),
        text(" "),
        cancel_button_->Render(),
    });
}

int RemoteFilesModalContent::GetCustomFooterHeight() const {
    if (error_footer_.empty()) {
        return 1;
    }
    // Count newlines plus one for the first line
    int lines = 1;
    for (char ch : error_footer_) {
        if (ch == '\n') {
            ++lines;
        }
    }
    return lines;
}

ftxui::Element RemoteFilesModalContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (error_footer_.empty()) {
        return text(" " + status_ + " ") |
            dim |
            color(theme.modal_text_color);
    }
    Elements lines;
    std::istringstream stream(error_footer_);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(text(" " + line + " ") |
            color(ftxui::Color::Red));
    }
    const int width = GetModalSizePreference().width - 2;
    return vbox(std::move(lines)) |
        size(WIDTH, EQUAL, std::max(1, width));
}
