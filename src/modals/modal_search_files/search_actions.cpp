void SearchFilesModalContent::UseMaskSet(size_t index) {
    if (mask_sets_.empty()) {
        selected_mask_set_ = 0;
        selected_filter_ = 0;
        masks_.clear();
        return;
    }

    selected_mask_set_ = std::min(index, mask_sets_.size() - 1);
    selected_filter_ = static_cast<int>(selected_mask_set_);
    masks_ = mask_sets_[selected_mask_set_].value;
}

void SearchFilesModalContent::ExecuteSearch(FileSearchTarget target) {
    status_.clear();
    SaveSelectedDirectoriesForCurrentRoot();
    const std::string save_status = status_;

    FileSearchOptions options;
    options.roots = SelectedRoots();
    options.query = query_;
    options.mask_set = mask_sets_[selected_mask_set_];
    options.mask_set.value = masks_;
    options.target = target;
    options.context_before = context_before_;
    options.context_after = context_after_;

    summary_ = engine_.Search(options);
    RebuildResultLabels();
    SelectTab(1);

    if (summary_.HasErrors() && summary_.matches.empty()) {
        status_ = summary_.FirstError();
    } else if (!save_status.empty()) {
        status_ = StatusText() + " | " + save_status;
    }
}

void SearchFilesModalContent::OpenSelectedMatch() {
    if (summary_.matches.empty()) {
        status_ = "No result selected.";
        return;
    }

    ClampResultSelection();

    if (!on_open_) {
        status_ = "Open callback is not configured.";
        return;
    }

    std::string error;
    const FileSearchMatch& match =
    summary_.matches[static_cast<size_t>(selected_result_)];

    if (!on_open_(match, error)) {
        status_ = error.empty() ? "Unable to open selected result." : error;
        return;
    }

    if (on_close_) {
        on_close_();
    }
}


void SearchFilesModalContent::PasteSearchQuery() {
    if (!read_clipboard_) {
        status_ = "Clipboard read is not configured.";
        return;
    }

    read_clipboard_([this](std::optional<std::string> text) {
        if (!text || text->empty()) {
            status_ = text ? "Clipboard has no text." : "Could not read the clipboard.";
            return;
        }
        query_ = ClipboardController::NormalizeText(*text, false);
        query_cursor_position_ = static_cast<int>(query_.size());
        status_ = "Search text pasted.";

        if (query_input_) {
            query_input_->TakeFocus();
        }
    });
}

void SearchFilesModalContent::ClearSearchQuery() {
    query_.clear();
    query_cursor_position_ = 0;
    status_ = "Search text cleared.";

    if (query_input_) {
        query_input_->TakeFocus();
    }
}

void SearchFilesModalContent::CopyResultText() {
    if (summary_.matches.empty()) {
        status_ = "No result selected.";
        return;
    }
    if (!write_clipboard_) {
        status_ = "Clipboard write is not configured.";
        return;
    }

    ClampResultSelection();
    const auto& match = summary_.matches[static_cast<size_t>(selected_result_)];
    if (match.line_number == 0) {
        status_ = write_clipboard_(ToDisplayPath(match.path.filename()))
            ? "Copied filename." : ClipboardController::kWriteFailed;
    } else {
        status_ = write_clipboard_(match.line_text)
            ? "Copied matching line." : ClipboardController::kWriteFailed;
    }
}

void SearchFilesModalContent::CopyResultPaths() {
    if (!write_clipboard_) {
        status_ = "Clipboard write is not configured.";
        return;
    }

    std::vector<std::string> paths;
    paths.reserve(summary_.matches.size());

    for (const FileSearchMatch& match : summary_.matches) {
        const std::string path = match.path.lexically_normal().generic_string();
        if (path.empty()) {
            continue;
        }
        if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
            paths.push_back(path);
        }
    }

    if (paths.empty()) {
        status_ = "No result paths to copy.";
        return;
    }

    std::ostringstream stream;
    for (size_t index = 0; index < paths.size(); ++index) {
        if (index > 0) {
            stream << '\n';
        }
        stream << paths[index];
    }

    status_ = write_clipboard_(stream.str())
        ? "Copied " + std::to_string(paths.size()) + " path(s)." : ClipboardController::kWriteFailed;
}

void SearchFilesModalContent::MoveResultSelection(int delta) {
    if (summary_.matches.empty()) {
        selected_result_ = 0;
        return;
    }

    const int max_index = static_cast<int>(summary_.matches.size() - 1);
    selected_result_ = std::max(0, std::min(max_index, selected_result_ + delta));
}

void SearchFilesModalContent::ClampResultSelection() {
    if (summary_.matches.empty()) {
        selected_result_ = 0;
        return;
    }

    const int max_index = static_cast<int>(summary_.matches.size() - 1);
    selected_result_ = std::max(0, std::min(max_index, selected_result_));
}

void SearchFilesModalContent::RebuildResultLabels() {
    result_labels_.clear();
    result_labels_.reserve(summary_.matches.size());

    for (const FileSearchMatch& match : summary_.matches) {
        std::string label = FormatLocation(match);
        if (match.line_number != 0) {
            label += "  " + TrimForDisplay(match.line_text, 170);
        }
        result_labels_.push_back(std::move(label));
    }

    selected_result_ = 0;
    ClampResultSelection();
}
