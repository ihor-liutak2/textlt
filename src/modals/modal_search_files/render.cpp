ftxui::Element SearchFilesModalContent::RenderSearchTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::vbox({
        ftxui::hbox({
            ftxui::text("Search: ") | ftxui::color(theme.modal_text_color),
            query_input_->Render() |
                ftxui::bgcolor(theme.modal_input_bg) |
                ftxui::color(theme.modal_input_fg) |
                ftxui::xflex,
            ftxui::text(" "),
            search_paste_button_->Render(),
            ftxui::text(" "),
            search_clear_button_->Render(),
        }),
        ftxui::text(""),
        ftxui::hbox({
            ftxui::text("Filter Mask: ") | ftxui::color(theme.modal_text_color),
            mask_picker_button_->Render(),
            ftxui::text("   Preview before match: ") | ftxui::color(theme.modal_text_color),
            context_before_input_component_->Render(),
            ftxui::text(" after: ") |
                ftxui::color(theme.modal_text_color),
            context_after_input_component_->Render(),
            ftxui::text("  (0–" + std::to_string(kMaxContextLines) + " lines each)") |
                ftxui::dim | ftxui::color(theme.modal_text_color),
        }),
        ftxui::separator() | ftxui::color(theme.modal_border),
        ftxui::hbox({
            ftxui::text("Directories: ") |
                ftxui::bold |
                ftxui::color(theme.modal_accent),
            ftxui::filler(),
            toggle_directory_button_->Render(),
            ftxui::text(" "),
            all_directories_button_->Render(),
            ftxui::text(" "),
            none_directories_button_->Render(),
        }),
        RenderDirectoryList() | ftxui::yflex,
    });
}

ftxui::Element SearchFilesModalContent::RenderResultsTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::vbox({
        RenderResultList() | ftxui::yflex,
        ftxui::separator() | ftxui::color(theme.modal_border),
        RenderSelectedResultPreview(),
    });
}

ftxui::Element SearchFilesModalContent::RenderDirectoryList() const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (directory_labels_.empty()) {
        return ftxui::text("No directories.") | ftxui::color(theme.modal_text_color);
    }

    return directory_list_component_->Render() |
        ftxui::vscroll_indicator |
        ftxui::frame;
}

ftxui::Element SearchFilesModalContent::RenderResultList() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (summary_.matches.empty()) {
        const std::string message = query_.empty()
            ? "Enter a query on the Search tab."
            : "No matches.";
        return ftxui::text(message) |
            ftxui::color(theme.modal_text_color) |
            ftxui::frame;
    }

    result_row_boxes_.resize(result_labels_.size());
    return result_list_component_->Render() |
        ftxui::vscroll_indicator |
        ftxui::frame |
        ftxui::reflect(result_list_box_);
}

ftxui::Element SearchFilesModalContent::RenderSelectedResultPreview() const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    if (summary_.matches.empty()) {
        return ftxui::text("No selected result.") |
            ftxui::color(theme.modal_text_color);
    }

    const int max_index = static_cast<int>(summary_.matches.size() - 1);
    const int safe_index = std::max(0, std::min(max_index, selected_result_));
    const FileSearchMatch& match = summary_.matches[static_cast<size_t>(safe_index)];

    if (match.line_number == 0) {
        return ftxui::text(ToDisplayPath(match.path)) | ftxui::color(theme.modal_accent);
    }

    ftxui::Elements rows;
    rows.push_back(
        ftxui::text(FormatLocation(match)) |
        ftxui::bold |
        ftxui::color(theme.modal_accent));

    for (const FileSearchContextLine& line : match.before) {
        rows.push_back(RenderContextLine(line));
    }

    rows.push_back(
        ftxui::text(
            "> " +
            FormatLineNumber(match.line_number) +
            " | " +
            TrimForDisplay(match.line_text, 170)) |
        ftxui::bgcolor(theme.modal_selected_item_bg) |
        ftxui::color(theme.modal_selected_item_fg) |
        ftxui::bold | ftxui::focus);

    for (const FileSearchContextLine& line : match.after) {
        rows.push_back(RenderContextLine(line));
    }

    return ftxui::vbox(std::move(rows)) |
        ftxui::yframe |
        ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 2 * kMaxContextLines + 2);
}

ftxui::Element SearchFilesModalContent::RenderContextLine(
    const FileSearchContextLine& line) const {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::text(
        "  " +
        FormatLineNumber(line.line_number) +
        " | " +
        TrimForDisplay(line.text, 170)) |
        ftxui::color(theme.modal_text_color) |
        ftxui::dim;
}

std::string SearchFilesModalContent::StatusText() const {
    std::ostringstream stream;
    stream << summary_.matches.size() << " match(es), "
           << summary_.files_with_matches << " file(s), "
           << summary_.files_scanned << " scanned";

    if (summary_.files_skipped > 0) {
        stream << ", " << summary_.files_skipped << " skipped";
    }

    if (summary_.truncated) {
        stream << ", truncated";
    }

    if (summary_.HasErrors()) {
        stream << ", warning: " << summary_.FirstError();
    }

    return stream.str();
}

std::string SearchFilesModalContent::DirectorySummaryText() const {
    size_t selected = 0;
    for (const DirectoryChoice& directory : directories_) {
        if (directory.selected) {
            ++selected;
        }
    }

    return std::to_string(selected) + " of " +
        std::to_string(directories_.size()) +
        (directories_.size() == 1 ? " directory selected" : " directories selected");
}

std::string SearchFilesModalContent::FormatLocation(
    const FileSearchMatch& match) const {
    if (match.line_number == 0) {
        return ToDisplayPath(match.relative_path);
    }
    return ToDisplayPath(match.relative_path) +
        ":" +
        std::to_string(match.line_number) +
        ":" +
        std::to_string(match.column);
}

std::string SearchFilesModalContent::FormatLineNumber(size_t line_number) const {
    std::string value = std::to_string(line_number);
    if (value.size() < 6) {
        value.insert(value.begin(), 6 - value.size(), ' ');
    }
    return value;
}

std::string SearchFilesModalContent::TrimForDisplay(
    const std::string& text,
    size_t max_size) const {
    if (text.size() <= max_size) {
        return text;
    }

    if (max_size <= 3) {
        return text.substr(0, max_size);
    }

    return text.substr(0, max_size - 3) + "...";
}
