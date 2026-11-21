ftxui::Element RemoteFilesModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string connection_label = CurrentConnectionLabel(connections_, selected_connection_);

    if (local_list_component_->Focused() || local_path_input_->Focused()) {
        active_panel_ = PanelSide::Local;
    } else if (remote_list_component_->Focused() || remote_path_input_->Focused()) {
        active_panel_ = PanelSide::Remote;
    }
    const auto panel_border = [&](PanelSide side) {
        return side == active_panel_ ? borderStyled(HEAVY, theme.modal_accent)
                                     : borderStyled(LIGHT, theme.modal_border);
    };

    return vbox({
        hbox({
            text(" Active connection: ") | color(theme.modal_accent),
            text(TrimForDisplay(connection_label, 36)) | bold | color(theme.modal_text_color),
            filler(),
            refresh_button_->Render(), text(" "),
            copy_error_button_->Render(), text(" "),
            connect_button_->Render(), text(" "),
            disconnect_button_->Render(),
        }),
        separator() | color(theme.modal_border),
        hbox({
            vbox({
                RenderRemoteDialogInputFrame(theme, "Local path", local_path_input_),
            }) | size(WIDTH, EQUAL, 58),
            separator(),
            vbox({
                RenderRemoteDialogInputFrame(theme, "Remote path", remote_path_input_),
            }) | size(WIDTH, EQUAL, 58),
        }),
        hbox({
            local_list_component_->Render() | reflect(local_panel_.bounds) | size(WIDTH, EQUAL, 58) | panel_border(PanelSide::Local),
            remote_list_component_->Render() | reflect(remote_panel_.bounds) | size(WIDTH, EQUAL, 58) | panel_border(PanelSide::Remote),
        }),
        RenderOperationRow(),
        hbox({
            text(" " + LastCachedLabel()) | dim | color(theme.modal_text_color),
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
        text(" " + panel.title + (active ? " (active) " : " ")) | bold,
        filler(),
        text(panel.status.empty() ? " " : TrimForDisplay(panel.status, 36)) |
            (panel.status_is_error ? color(ftxui::Color::Red) : nothing),
    }) | bgcolor(active ? theme.modal_selected_item_bg : theme.modal_background) |
         color(active ? theme.modal_selected_item_fg : theme.modal_text_color));
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

    if (selected && side == active_panel_) {
        row = row |
            bgcolor(theme.modal_selected_item_bg) |
            color(theme.modal_selected_item_fg);
    } else if (selected) {
        row = row | underlined | color(theme.modal_text_color);
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

    const bool overwrite = pending_operation_ == PendingOperation::CopyToLocalOverwrite ||
        pending_operation_ == PendingOperation::CopyToRemoteOverwrite;
    return hbox({
        RenderRemoteDialogInputFrame(theme, pending_input_label_, operation_input_) | size(WIDTH, EQUAL, overwrite ? 24 : 44),
        text(" "),
        vbox({
            text(""),
            text(""),
            hbox({confirm_button_->Render(), text(" "), cancel_button_->Render()}),
            filler(),
        }),
        text(" "),
        overwrite ? vbox({text(pending_source_details_), text(pending_target_details_)}) : filler(),
    });
}

int RemoteFilesModalContent::GetCustomFooterHeight() const {
    return 3;
}

ftxui::Element RemoteFilesModalContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        text(" " + status_.substr(0, status_.find_first_of("\r\n"))) |
            color(status_is_error_ ? Color::Red : theme.modal_text_color),
        separator() | color(theme.modal_border),
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
    });
}
