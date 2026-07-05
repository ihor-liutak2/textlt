ftxui::Element FilesModalContent::RenderTopButtons() {
    using namespace ftxui;
    return hbox({
        home_button_->Render(),
        text(" "),
        documents_button_->Render(),
        text(" "),
        downloads_button_->Render(),
        text(" "),
        current_dir_button_->Render(),
        text(" "),
        add_dir_button_->Render(),
        text(" "),
        refresh_button_->Render(),
        text(" "),
        copy_path_button_->Render(),
    });
}

ftxui::Element FilesModalContent::RenderFavoriteDirectories() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    favorite_boxes_.assign(favorite_directories_.size(), {});
    if (favorite_directories_.empty()) {
        return text(" Added directories: none") |
            dim |
            color(theme.modal_text_color);
    }

    Elements buttons;
    buttons.push_back(text(" ") | color(theme.modal_text_color));
    for (size_t index = 0; index < favorite_directories_.size(); ++index) {
        if (index > 0) {
            buttons.push_back(text(" "));
        }
        std::string label = FileManager::PathToUtf8(favorite_directories_[index].filename());
        if (label.empty()) {
            label = FileManager::PathToUtf8(favorite_directories_[index]);
        }
        ButtonSpec spec;
        spec.caption = TrimForDisplay(label, 16);
        spec.role = ButtonRole::Navigation;
        spec.variant = ButtonVariant::ColoredBrackets;
        spec.size = ButtonSize::Compact;
        buttons.push_back(RenderButton(theme, spec) | reflect(favorite_boxes_[index]));
    }
    return hbox(std::move(buttons));
}

ftxui::Element FilesModalContent::RenderPathInput() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Path: ") | bold | color(theme.modal_accent),
        path_input_->Render() | xflex | bgcolor(theme.modal_input_bg),
    });
}

ftxui::Element FilesModalContent::RenderFileNameInput() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    if (!IsSaveLikeMode()) {
        return text("");
    }
    return hbox({
        text(" File: ") | bold | color(theme.modal_accent),
        file_name_input_->Render() | xflex | bgcolor(theme.modal_input_bg),
    });
}

ftxui::Element FilesModalContent::RenderOperationButtons() {
    using namespace ftxui;
    return hbox({
        create_dir_button_->Render(),
        text(" "),
        create_file_button_->Render(),
        text(" "),
        delete_button_->Render(),
        text(" "),
        rename_button_->Render(),
        text(" "),
        copy_button_->Render(),
        text(" "),
        cut_button_->Render(),
        text(" "),
        paste_button_->Render(),
    });
}

ftxui::Element FilesModalContent::RenderConfirmOverlay() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows = {
        text("Confirm " + PendingOperationActionLabel()) | bold,
        separator(),
        paragraph(pending_operation_message_) | color(theme.modal_text_color),
    };

    if (PendingOperationNeedsInput()) {
        rows.push_back(text(pending_operation_input_label_ + ":") | color(theme.modal_accent));
        rows.push_back(operation_input_->Render() |
            border |
            size(WIDTH, EQUAL, 42) |
            bgcolor(theme.modal_input_bg));
    }

    rows.push_back(hbox({
        confirm_yes_button_->Render(),
        text(" "),
        confirm_cancel_button_->Render(),
    }));
    return vbox(std::move(rows)) |
        size(WIDTH, LESS_THAN, 66) |
        border |
        bgcolor(theme.modal_background) |
        color(theme.modal_foreground);
}

ftxui::Element FilesModalContent::RenderEntryList() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    entry_boxes_.assign(0, {});

    Elements rows;
    rows.push_back(
        hbox({
            text("  Type ") | bold | size(WIDTH, EQUAL, 8),
            text("Name") | bold | size(WIDTH, EQUAL, kEntryNameWidth),
            text("Size") | bold | size(WIDTH, EQUAL, kEntrySizeWidth),
        }) | color(theme.modal_accent));
    rows.push_back(separator() | color(theme.modal_border));

    if (entries_.empty()) {
        rows.push_back(text("  No files or directories.") | color(theme.modal_text_color));
    } else {
        EnsureSelectionVisible();
        const int end = std::min(
            static_cast<int>(entries_.size()),
            scroll_offset_ + kVisibleEntryRows);
        entry_boxes_.assign(static_cast<size_t>(end - scroll_offset_), {});
        for (int index = scroll_offset_; index < end; ++index) {
            const FileEntry& entry = entries_[static_cast<size_t>(index)];
            const size_t visible_index = static_cast<size_t>(index - scroll_offset_);
            const bool cursor_here = index == selected_entry_;
            const bool marked = IsEntryMarkedSelected(index);
            std::string marker = "  ";
            if (cursor_here && marked) {
                marker = ">*";
            } else if (cursor_here) {
                marker = "> ";
            } else if (marked) {
                marker = " *";
            }

            Element row = hbox({
                text(marker),
                text(FileTypeLabel(entry.type)) | size(WIDTH, EQUAL, 6),
                text(FormatEntryName(entry, kEntryNameWidth)) |
                    size(WIDTH, EQUAL, kEntryNameWidth),
                text(FormatSize(entry)) | size(WIDTH, EQUAL, kEntrySizeWidth),
            }) | reflect(entry_boxes_[visible_index]);

            if (cursor_here) {
                row = row |
                    bgcolor(theme.modal_selected_item_bg) |
                    color(theme.modal_selected_item_fg) |
                    bold;
            } else if (marked) {
                row = row |
                    bgcolor(theme.modal_input_bg) |
                    color(theme.modal_text_color) |
                    bold;
            } else {
                row = row | color(theme.modal_text_color);
            }
            rows.push_back(row);
        }
    }

    while (static_cast<int>(rows.size()) < kVisibleEntryRows + 2) {
        rows.push_back(text(""));
    }

    return vbox(std::move(rows)) |
        size(HEIGHT, EQUAL, kVisibleEntryRows + 2) |
        frame;
}

ftxui::Element FilesModalContent::RenderSelectionSummary() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    std::string text_value = "No selection";
    if (!entries_.empty()) {
        const int safe_index = std::clamp(
            selected_entry_,
            0,
            static_cast<int>(entries_.size()) - 1);
        const FileEntry& entry = entries_[static_cast<size_t>(safe_index)];
        const size_t marked_count = SortedSelectedIndices().size();
        if (marked_count > 0) {
            text_value = "Selected " + std::to_string(marked_count) +
                " item(s), cursor: " + FileManager::PathToUtf8(entry.path);
        } else {
            text_value = "Selected: " + FileManager::PathToUtf8(entry.path);
        }
    }
    const std::string range = entries_.empty()
        ? "0/0"
        : std::to_string(selected_entry_ + 1) + "/" + std::to_string(entries_.size());
    return hbox({
        text(" " + TrimForDisplay(text_value, 92)) |
            dim |
            color(theme.modal_text_color),
        filler(),
        text(" " + range + " ") |
            dim |
            color(theme.modal_text_color),
    });
}

ftxui::Element FilesModalContent::RenderStatusSummary() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string prefix = status_is_error_ ? "Error: " : "Status: ";
    Element line = text(" " + TrimForDisplay(prefix + status_, 104));
    if (status_is_error_) {
        line = line | color(Color::Red) | bold;
    } else {
        line = line | dim | color(theme.modal_text_color);
    }
    return line;
}

ftxui::Element FilesModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows = {
        RenderTopButtons(),
        RenderFavoriteDirectories(),
        RenderPathInput(),
    };
    if (IsSaveLikeMode()) {
        rows.push_back(RenderFileNameInput());
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(RenderOperationButtons());
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(RenderEntryList());
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(RenderSelectionSummary());
    rows.push_back(RenderStatusSummary());

    Element body = vbox(std::move(rows)) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color);
    if (!HasPendingOperation()) {
        return body;
    }
    return dbox({
        body,
        vbox({
            filler(),
            hbox({filler(), RenderConfirmOverlay(), filler()}),
            filler(),
        }),
    });
}

std::string FilesModalContent::ModeTitle() const {
    switch (mode_) {
        case FilesModalMode::Open:
            return "Open";
        case FilesModalMode::SaveAs:
            return "Save As";
        case FilesModalMode::Import:
            return "Import";
        case FilesModalMode::Manage:
            return "Files";
        case FilesModalMode::None:
            break;
    }
    return "Files";
}

std::string FilesModalContent::FooterActionLabel() const {
    switch (mode_) {
        case FilesModalMode::Open:
            return "Open";
        case FilesModalMode::SaveAs:
            return "Save As";
        case FilesModalMode::Import:
            return "Import";
        case FilesModalMode::Manage:
        case FilesModalMode::None:
            break;
    }
    return "Confirm";
}

std::string FilesModalContent::FormatCurrentDirectory() const {
    if (current_directory_.empty()) {
        return FileManager::PathToUtf8(std::filesystem::current_path());
    }
    return FileManager::PathToUtf8(current_directory_);
}

std::string FilesModalContent::FormatEntryName(const FileEntry& entry, size_t width) const {
    std::string name;
    if (IsParentEntry(entry)) {
        name = "../";
    } else if (entry.type == FileEntryType::Directory) {
        name = entry.name + "/";
    } else {
        name = entry.name;
    }
    return TrimForDisplay(name, width);
}

std::string FilesModalContent::FormatSize(const FileEntry& entry) const {
    if (entry.type == FileEntryType::Directory) {
        return "<DIR>";
    }
    if (entry.type == FileEntryType::Symlink) {
        return "<LINK>";
    }
    if (entry.type != FileEntryType::File) {
        return "";
    }
    return FileManager::FormatFileSize(entry.size);
}

std::string FilesModalContent::TrimForDisplay(const std::string& text, size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }
    if (max_size <= 3) {
        return text.substr(0, max_size);
    }
    return text.substr(0, max_size - 3) + "...";
}

std::string FilesModalContent::SuggestedFileNameFromPath(
    const std::filesystem::path& path) const {
    return FileManager::PathToUtf8(path.filename());
}
