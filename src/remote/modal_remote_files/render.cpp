ModalSizePreference RemoteFilesModalContent::GetModalSizePreference() const {
    const auto terminal = ftxui::Terminal::Size();
    return {std::min(156, terminal.dimx), std::min(42, terminal.dimy)};
}

class RemoteFilesModalContent::PanelEntries final : public ftxui::Node {
public:
    PanelEntries(RemoteFilesModalContent& content, PanelSide side, const Theme& theme)
        : content_(content), side_(side), theme_(theme) {}

    void ComputeRequirement() override { requirement_.min_y = 2; }

    void SetBox(ftxui::Box box) override {
        using namespace ftxui;
        Node::SetBox(box);
        children_.clear();

        auto& panel = content_.Panel(side_);
        panel.visible_rows = box.IsEmpty() ? 0 : box.y_max - box.y_min;
        panel.scrollbar_box = {1, 0, 1, 0};
        if (box.IsEmpty()) {
            panel.scrollbar_drag.reset();
            return;
        }

        const int width = BoxWidth(box);
        const bool show_details = width >= kDetailsMinPanelWidth;
        const int name_width = std::max(0, width - kNamePrefixWidth -
            (show_details ? kTypeWidth + kSizeWidth : 0));
        panel.scrollbar_box = {box.x_min + kNamePrefixWidth, box.x_min + kNamePrefixWidth + name_width - 1,
            box.y_max, box.y_max};

        content_.SelectEntry(side_, panel.selected);
        const int widest_name = panel.entries.empty() ? 0 : panel.widest_name;
        panel.name_scroll_offset = std::clamp(panel.name_scroll_offset, 0,
            std::max(0, widest_name - name_width));
        const ScrollbarMetrics bar(name_width, widest_name, panel.name_scroll_offset);

        for (int row = 0; row < panel.visible_rows; ++row) {
            const auto index = static_cast<size_t>(panel.scroll_offset + row);
            if (index < panel.entries.size()) {
                AddRow(content_.RenderEntryRow(side_, index, show_details),
                    {box.x_min, box.x_max, box.y_min + row, box.y_min + row});
            } else if (panel.entries.empty() && row == panel.visible_rows / 2) {
                AddRow(text("No entries") | center | color(theme_.modal_text_color),
                    {box.x_min, box.x_max, box.y_min + row, box.y_min + row});
            }
        }

        if (bar.max_offset == 0) {
            panel.scrollbar_drag.reset();
        }
        if (bar.length > 0) {
            const auto track = [this](int length, bool thumb = false) {
                std::string line;
                for (int col = 0; col < length; ++col) {
                    line += thumb ? "━" : "─";
                }
                return text(std::move(line)) | color(thumb ? theme_.modal_accent : theme_.modal_border);
            };
            AddRow(hbox({
                track(bar.thumb_start),
                track(bar.thumb_length, true),
                track(bar.length - bar.thumb_start - bar.thumb_length),
            }), panel.scrollbar_box);
        }
    }

private:
    void AddRow(ftxui::Element row, ftxui::Box box) {
        row->ComputeRequirement();
        row->SetBox(box);
        children_.push_back(std::move(row));
    }

    RemoteFilesModalContent& content_;
    PanelSide side_;
    Theme theme_;
};

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

    const int body_width = std::max(18, GetModalSizePreference().width - 2);
    const int left_width = body_width / 2;
    const int right_width = body_width - left_width;
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
            RenderRemoteDialogInputFrame(theme, "Local path", local_path_input_) |
                size(WIDTH, EQUAL, left_width),
            RenderRemoteDialogInputFrame(theme, "Remote path", remote_path_input_) |
                size(WIDTH, EQUAL, right_width),
        }),
        hbox({
            local_list_component_->Render() | reflect(local_panel_.bounds) |
                panel_border(PanelSide::Local) | size(WIDTH, EQUAL, left_width),
            remote_list_component_->Render() | reflect(remote_panel_.bounds) |
                panel_border(PanelSide::Remote) | size(WIDTH, EQUAL, right_width),
        }) | yflex_grow,
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

    return vbox({
        hbox({
            text(" " + panel.title + (active ? " (active) " : " ")) | bold,
            filler(),
            text(TrimForDisplay(panel.status, 36)) | xflex_shrink |
                (panel.status_is_error ? color(Color::Red) : nothing),
        }) | bgcolor(active ? theme.modal_selected_item_bg : theme.modal_background) |
             color(active ? theme.modal_selected_item_fg : theme.modal_text_color),
        separator() | color(theme.modal_border),
        std::make_shared<PanelEntries>(*this, side, theme) | yflex_grow,
    }) | bgcolor(theme.modal_input_bg) | color(theme.modal_input_fg);
}

ftxui::Element RemoteFilesModalContent::RenderEntryRow(PanelSide side, size_t index, bool show_details) {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    PanelState& panel = Panel(side);
    const RemoteEntry& entry = panel.entries[index];
    const bool selected = static_cast<int>(index) == panel.selected;
    const std::string prefix = entry.type == RemoteEntryType::Directory ? "[D] " : "    ";

    const int name_width = BoxWidth(panel.scrollbar_box);
    Elements columns{
        text(" " + prefix) | bold | size(WIDTH, EQUAL, kNamePrefixWidth),
        hbox({
            text(VisibleLabel(entry.name, panel.name_scroll_offset, name_width)) | bold,
            filler(),
        }) | size(WIDTH, EQUAL, name_width),
    };
    if (show_details) {
        columns.push_back(text(EntryTypeLabel(entry.type)) | dim | size(WIDTH, EQUAL, kTypeWidth));
        columns.push_back(text(EntrySizeLabel(entry)) | dim | size(WIDTH, EQUAL, kSizeWidth));
    }
    Element row = hbox(std::move(columns)) | reflect(panel.boxes[index]);

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
            text(" Enter opens. Backspace goes up. F5 copies (overwrite requires confirmation). Ctrl+Left/Right scrolls names. ") |
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
