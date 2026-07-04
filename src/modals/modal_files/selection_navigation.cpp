std::vector<std::filesystem::path> FilesModalContent::SelectedOperationPaths(
    std::string& error) const {
    std::vector<std::filesystem::path> paths;
    const std::vector<int> indices = SortedSelectedIndices();
    if (!indices.empty()) {
        for (int index : indices) {
            if (index < 0 || index >= static_cast<int>(entries_.size())) {
                continue;
            }
            const FileEntry& entry = entries_[static_cast<size_t>(index)];
            if (!IsParentEntry(entry)) {
                paths.push_back(entry.path);
            }
        }
        if (!paths.empty()) {
            return paths;
        }
    }

    std::filesystem::path path;
    if (!SelectedPath(path, error)) {
        return {};
    }
    paths.push_back(path);
    error.clear();
    return paths;
}

std::vector<int> FilesModalContent::SortedSelectedIndices() const {
    std::vector<int> indices;
    indices.reserve(selected_indices_.size());
    for (int index : selected_indices_) {
        if (index >= 0 && index < static_cast<int>(entries_.size())) {
            indices.push_back(index);
        }
    }
    return indices;
}

void FilesModalContent::ClearSelectionMarks() {
    selected_indices_.clear();
    selection_anchor_ = -1;
}

void FilesModalContent::ToggleEntrySelection(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return;
    }
    if (IsParentEntry(entries_[static_cast<size_t>(index)])) {
        return;
    }
    if (selected_indices_.count(index) > 0) {
        selected_indices_.erase(index);
    } else {
        selected_indices_.insert(index);
    }
    selection_anchor_ = index;
    SelectEntry(index);
}

void FilesModalContent::SelectRangeTo(int index) {
    if (entries_.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(entries_.size()) - 1);
    if (selection_anchor_ < 0 || selection_anchor_ >= static_cast<int>(entries_.size())) {
        selection_anchor_ = selected_entry_;
    }
    const int begin = std::min(selection_anchor_, index);
    const int end = std::max(selection_anchor_, index);
    selected_indices_.clear();
    for (int current = begin; current <= end; ++current) {
        if (!IsParentEntry(entries_[static_cast<size_t>(current)])) {
            selected_indices_.insert(current);
        }
    }
    SelectEntry(index);
}

bool FilesModalContent::IsEntryMarkedSelected(int index) const {
    return selected_indices_.count(index) > 0;
}

void FilesModalContent::MoveSelectionWithRange(int delta) {
    if (entries_.empty()) {
        return;
    }
    const int target = std::clamp(
        selected_entry_ + delta,
        0,
        static_cast<int>(entries_.size()) - 1);
    SelectRangeTo(target);
}

void FilesModalContent::ConfirmSelected() {
    if (!IsFileActionMode()) {
        SetStatus("Files mode is for navigation and file operations.");
        return;
    }

    std::filesystem::path target;
    std::string error;
    if (!TargetPathForMode(target, error)) {
        SetStatus(error, true);
        return;
    }

    if (on_confirm_path_ && !on_confirm_path_(mode_, target, error)) {
        SetStatus(error.empty() ? "Operation failed." : error, true);
        return;
    }

    SetStatus(FooterActionLabel() + " complete: " + FileManager::PathToUtf8(target));
    if (on_close_) {
        on_close_();
    }
}

void FilesModalContent::NavigateUp() {
    if (current_directory_.empty()) {
        return;
    }
    const std::filesystem::path parent = current_directory_.parent_path();
    if (parent.empty() || parent == current_directory_) {
        SetStatus("Already at filesystem root.");
        return;
    }
    LoadDirectory(parent);
}

void FilesModalContent::ActivateSelected(bool double_click) {
    if (entries_.empty()) {
        SetStatus("No item selected.", true);
        return;
    }

    ClampSelection();
    const FileEntry& entry = entries_[static_cast<size_t>(selected_entry_)];
    if (entry.type == FileEntryType::Directory) {
        LoadDirectory(entry.path);
        return;
    }

    if (IsSaveLikeMode()) {
        file_name_input_value_ = entry.name;
        file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        SetStatus("Selected target file name: " + entry.name);
        return;
    }

    if (double_click || mode_ == FilesModalMode::Open || mode_ == FilesModalMode::Import) {
        ConfirmSelected();
    }
}

void FilesModalContent::SelectEntry(int index) {
    if (entries_.empty()) {
        selected_entry_ = 0;
        scroll_offset_ = 0;
        return;
    }
    selected_entry_ = std::clamp(index, 0, static_cast<int>(entries_.size()) - 1);
    EnsureSelectionVisible();
    if (IsSaveLikeMode()) {
        const FileEntry& entry = entries_[static_cast<size_t>(selected_entry_)];
        if (entry.type != FileEntryType::Directory) {
            file_name_input_value_ = entry.name;
            file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        }
    }
}

void FilesModalContent::MoveSelection(int delta) {
    if (entries_.empty()) {
        selected_entry_ = 0;
        scroll_offset_ = 0;
        return;
    }
    SelectEntry(selected_entry_ + delta);
}

void FilesModalContent::ClampSelection() {
    if (entries_.empty()) {
        selected_entry_ = 0;
        return;
    }
    selected_entry_ = std::clamp(selected_entry_, 0, static_cast<int>(entries_.size()) - 1);
}

void FilesModalContent::EnsureSelectionVisible() {
    if (entries_.empty()) {
        scroll_offset_ = 0;
        return;
    }
    ClampSelection();
    if (selected_entry_ < scroll_offset_) {
        scroll_offset_ = selected_entry_;
    }
    if (selected_entry_ >= scroll_offset_ + kVisibleEntryRows) {
        scroll_offset_ = selected_entry_ - kVisibleEntryRows + 1;
    }
    const int max_offset = std::max(0, static_cast<int>(entries_.size()) - kVisibleEntryRows);
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_offset);
}

void FilesModalContent::SetStatus(std::string message, bool error) {
    status_ = std::move(message);
    status_is_error_ = error;
}

bool FilesModalContent::SelectedPath(std::filesystem::path& path, std::string& error) const {
    if (entries_.empty()) {
        error = "No file selected.";
        return false;
    }
    const int safe_index = std::clamp(
        selected_entry_,
        0,
        static_cast<int>(entries_.size()) - 1);
    const FileEntry& entry = entries_[static_cast<size_t>(safe_index)];
    if (IsParentEntry(entry)) {
        error = "Selected item is parent directory.";
        return false;
    }
    path = entry.path;
    return true;
}

bool FilesModalContent::TargetPathForMode(std::filesystem::path& path, std::string& error) const {
    if (IsSaveLikeMode()) {
        if (file_name_input_value_.empty()) {
            error = "Enter a file name.";
            return false;
        }
        if (!FileManager::IsPlainName(file_name_input_value_)) {
            error = "Enter only a file name.";
            return false;
        }
        path = current_directory_ / FileManager::PathFromUtf8(file_name_input_value_);
        return true;
    }

    if (!SelectedPath(path, error)) {
        return false;
    }

    std::error_code status_error;
    if (std::filesystem::is_directory(path, status_error)) {
        error = "Selected item is a directory.";
        return false;
    }
    return true;
}

FileFilter FilesModalContent::FilterForMode() const {
    FileFilter filter;
    filter.show_directories = true;
    filter.show_files = true;
    filter.show_hidden = true;
    switch (mode_) {
        case FilesModalMode::Import:
            filter.extensions = {".docx", ".fb2", ".fb2.zip", ".rtf", ".odt", ".ott", ".gdoc"};
            break;
        case FilesModalMode::None:
        case FilesModalMode::Open:
        case FilesModalMode::SaveAs:
        case FilesModalMode::Manage:
            break;
    }
    return filter;
}

bool FilesModalContent::IsParentEntry(const FileEntry& entry) const {
    return entry.type == FileEntryType::Directory && entry.name == "..";
}

bool FilesModalContent::IsFileActionMode() const {
    return mode_ == FilesModalMode::Open ||
        mode_ == FilesModalMode::SaveAs ||
        mode_ == FilesModalMode::Import;
}

bool FilesModalContent::IsSaveLikeMode() const {
    return mode_ == FilesModalMode::SaveAs;
}
