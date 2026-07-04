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

void SearchFilesModalContent::UseFirstMaskSet() {
    UseMaskSet(0);
    status_ = "Mask set: " + mask_sets_[selected_mask_set_].name;
}

void SearchFilesModalContent::UsePreviousMaskSet() {
    if (mask_sets_.empty()) {
        return;
    }

    if (selected_mask_set_ == 0) {
        selected_mask_set_ = mask_sets_.size() - 1;
    } else {
        --selected_mask_set_;
    }

    masks_ = mask_sets_[selected_mask_set_].value;
    status_ = "Mask set: " + mask_sets_[selected_mask_set_].name;
}

void SearchFilesModalContent::UseNextMaskSet() {
    if (mask_sets_.empty()) {
        return;
    }

    selected_mask_set_ = (selected_mask_set_ + 1) % mask_sets_.size();
    masks_ = mask_sets_[selected_mask_set_].value;
    status_ = "Mask set: " + mask_sets_[selected_mask_set_].name;
}

void SearchFilesModalContent::ExecuteSearch() {
    SaveSelectedDirectoriesForCurrentRoot();

    FileSearchOptions options;
    options.roots = SelectedRoots();
    options.query = query_;

    options.mask_set = mask_sets_.empty()
        ? FileSearchEngine::DefaultCodeMaskSet()
        : mask_sets_[selected_mask_set_];
    options.mask_set.value = masks_;

    options.context_before = ParseContextValue(context_before_input_);
    options.context_after = ParseContextValue(context_after_input_);

    summary_ = engine_.Search(options);
    RebuildResultLabels();

    selected_tab_ = 1;

    if (result_list_component_) {
        result_list_component_->TakeFocus();
    }

    if (summary_.HasErrors() && summary_.matches.empty()) {
        status_ = summary_.FirstError();
    } else {
        status_ = StatusText();
    }

    if (open_button_) {
        open_button_->TakeFocus();
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

    const std::string text = read_clipboard_();
    if (text.empty()) {
        status_ = "Clipboard is empty.";
        return;
    }

    query_ = text;
    query_cursor_position_ = static_cast<int>(query_.size());
    status_ = "Search text pasted.";

    if (query_input_) {
        query_input_->TakeFocus();
    }
}

void SearchFilesModalContent::ClearSearchQuery() {
    query_.clear();
    query_cursor_position_ = 0;
    status_ = "Search text cleared.";

    if (query_input_) {
        query_input_->TakeFocus();
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

    write_clipboard_(stream.str());
    status_ = "Copied " + std::to_string(paths.size()) + " path(s).";
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
        result_labels_.push_back(
            FormatLocation(match) +
            "  " +
            TrimForDisplay(match.line_text, 170));
    }

    selected_result_ = 0;
    ClampResultSelection();
}

size_t SearchFilesModalContent::ParseContextValue(const std::string& value) const {
    if (!IsDigitsOnly(value)) {
        return 0;
    }

    try {
        return std::min<size_t>(static_cast<size_t>(std::stoul(value)), 20);
    } catch (const std::exception&) {
        return 0;
    }
}
