void FilesModalContent::Open(
    FilesModalMode mode,
    const std::filesystem::path& start_path,
    std::string suggested_file_name) {
    mode_ = mode;
    status_is_error_ = false;
    status_ = "Ready.";
    CancelPendingOperation();
    ClearSelectionMarks();
    file_name_input_value_ = std::move(suggested_file_name);
    file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
    RefreshFavorites();

    std::filesystem::path directory = start_path;
    std::error_code error_code;
    if (directory.empty()) {
        if (start_directory_provider_) {
            directory = start_directory_provider_();
        }
    }
    if (directory.empty()) {
        directory = std::filesystem::current_path(error_code);
    }
    if (!std::filesystem::is_directory(directory, error_code)) {
        const std::string file_name = SuggestedFileNameFromPath(directory);
        if (file_name_input_value_.empty()) {
            file_name_input_value_ = file_name;
            file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        }
        directory = directory.parent_path();
    }
    if (directory.empty() || !std::filesystem::is_directory(directory, error_code)) {
        directory = std::filesystem::current_path(error_code);
    }

    LoadDirectory(directory);
    if (entry_list_component_) {
        entry_list_component_->TakeFocus();
    }
}

void FilesModalContent::Close() {
    mode_ = FilesModalMode::None;
    entries_.clear();
    favorite_boxes_.clear();
    entry_boxes_.clear();
    ClearSelectionMarks();
    CancelPendingOperation();
    last_clicked_entry_ = -1;
}

std::string FilesModalContent::GetTitle() {
    return ModeTitle();
}

ftxui::Element FilesModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

void FilesModalContent::Refresh() {
    LoadDirectory(current_directory_);
}

void FilesModalContent::RefreshFavorites() {
    favorite_directories_.clear();
    if (favorite_directories_provider_) {
        favorite_directories_ = favorite_directories_provider_();
    }
}

void FilesModalContent::LoadDirectory(const std::filesystem::path& directory) {
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }

    try {
        std::error_code status_error;
        std::filesystem::path target = directory;
        if (target.empty()) {
            target = std::filesystem::current_path(status_error);
            if (status_error) {
                SetStatus(status_error.message(), true);
                return;
            }
        }
        if (!std::filesystem::is_directory(target, status_error) || status_error) {
            SetStatus("Directory does not exist: " + FileManager::PathToUtf8(target), true);
            return;
        }

        std::vector<FileEntry> listed_entries;
        std::string error;
        if (!file_manager_->ListDirectory(target, FilterForMode(), listed_entries, error)) {
            SetStatus(error.empty() ? "Unable to read directory." : error, true);
            return;
        }

        current_directory_ = target.lexically_normal();
        entries_.clear();
        ClearSelectionMarks();

        const std::filesystem::path parent = current_directory_.parent_path();
        if (!parent.empty() && parent != current_directory_) {
            FileEntry parent_entry;
            parent_entry.path = parent;
            parent_entry.name = "..";
            parent_entry.type = FileEntryType::Directory;
            entries_.push_back(std::move(parent_entry));
        }

        entries_.insert(entries_.end(), listed_entries.begin(), listed_entries.end());
        selected_entry_ = entries_.empty() ? 0 : std::min(selected_entry_, static_cast<int>(entries_.size()) - 1);
        scroll_offset_ = 0;
        path_input_value_ = FileManager::PathToUtf8(current_directory_);
        path_input_cursor_ = static_cast<int>(path_input_value_.size());
        EnsureSelectionVisible();

        if (entries_.empty()) {
            SetStatus("Directory is empty.");
        } else {
            SetStatus("Enter opens folders or confirms selected file. Double click works too.");
        }
    } catch (const std::exception& e) {
        SetStatus(std::string("Unable to read directory: ") + e.what(), true);
    } catch (...) {
        SetStatus("Unable to read directory.", true);
    }
}

void FilesModalContent::LoadPathFromInput() {
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }

    try {
    std::string error;
    const std::filesystem::path resolved = FileManager::ResolvePath(
        path_input_value_,
        current_directory_,
        error);
    if (!error.empty()) {
        SetStatus(error, true);
        return;
    }

    std::error_code status_error;
    if (std::filesystem::is_directory(resolved, status_error)) {
        LoadDirectory(resolved);
        return;
    }

    if (std::filesystem::is_regular_file(resolved, status_error)) {
        LoadDirectory(resolved.parent_path());
        const std::string name = FileManager::PathToUtf8(resolved.filename());
        for (size_t index = 0; index < entries_.size(); ++index) {
            if (entries_[index].name == name) {
                SelectEntry(static_cast<int>(index));
                break;
            }
        }
        if (IsSaveLikeMode()) {
            file_name_input_value_ = name;
            file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        }
        return;
    }

    if (IsSaveLikeMode() && !resolved.parent_path().empty() &&
        std::filesystem::is_directory(resolved.parent_path(), status_error)) {
        LoadDirectory(resolved.parent_path());
        file_name_input_value_ = FileManager::PathToUtf8(resolved.filename());
        file_name_input_cursor_ = static_cast<int>(file_name_input_value_.size());
        SetStatus("Target file name prepared: " + file_name_input_value_);
        return;
    }

    SetStatus("Path is not a directory or file: " + FileManager::PathToUtf8(resolved), true);
    } catch (const std::exception& e) {
        SetStatus(std::string("Unable to load path: ") + e.what(), true);
    } catch (...) {
        SetStatus("Unable to load path.", true);
    }
}

void FilesModalContent::LoadBuiltInDirectory(const std::filesystem::path& directory) {
    if (directory.empty()) {
        SetStatus("Directory is not available.", true);
        return;
    }
    LoadDirectory(directory);
}

void FilesModalContent::AddCurrentDirectoryToFavorites() {
    if (current_directory_.empty()) {
        SetStatus("No active directory to add.", true);
        return;
    }
    std::string error;
    if (on_add_favorite_directory_ &&
        !on_add_favorite_directory_(current_directory_, error)) {
        SetStatus(error.empty() ? "Directory was not added." : error, true);
        return;
    }
    RefreshFavorites();
    SetStatus("Added directory: " + FileManager::PathToUtf8(current_directory_));
}

void FilesModalContent::CopySelectedPathText() {
    std::string error;
    std::vector<std::filesystem::path> paths = SelectedOperationPaths(error);
    if (!error.empty() && !current_directory_.empty()) {
        paths.clear();
        paths.push_back(current_directory_);
    }

    const std::string text = FileManager::BuildPathText(paths);
    if (text.empty()) {
        SetStatus("Nothing to copy.", true);
        return;
    }
    if (on_copy_path_) {
        on_copy_path_(text);
    }
    SetStatus("Copied path text.");
}
