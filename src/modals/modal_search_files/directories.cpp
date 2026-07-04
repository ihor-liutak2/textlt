void SearchFilesModalContent::BuildDirectoryChoices() {
    directories_.clear();
    directory_labels_.clear();
    selected_directory_ = 0;
    current_root_path_.clear();

    std::vector<FileSearchRoot> roots;
    if (root_provider_) {
        roots = root_provider_();
    }

    std::vector<std::string> seen_paths;

    for (const FileSearchRoot& raw_root : roots) {
        std::error_code error;
        std::filesystem::path absolute_root =
            std::filesystem::absolute(raw_root.path, error).lexically_normal();
        if (error || absolute_root.empty()) {
            continue;
        }

        std::error_code status_error;
        if (!std::filesystem::is_directory(absolute_root, status_error)) {
            continue;
        }

        if (current_root_path_.empty()) {
            current_root_path_ = absolute_root;
        }

        std::filesystem::directory_iterator iterator(
            absolute_root,
            std::filesystem::directory_options::skip_permission_denied,
            error);

        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;

            std::error_code entry_error;
            if (entry.is_directory(entry_error)) {
                const std::string directory_name = entry.path().filename().string();
                if (!IsIgnoredDirectoryName(directory_name)) {
                    AddDirectoryChoice(
                        FileSearchRoot{entry.path(), directory_name},
                        true,
                        &seen_paths);
                }
            }

            iterator.increment(error);
        }
    }

    if (directories_.empty()) {
        for (const FileSearchRoot& raw_root : roots) {
            std::error_code error;
            std::filesystem::path absolute_root =
                std::filesystem::absolute(raw_root.path, error).lexically_normal();
            if (!error && !absolute_root.empty()) {
                FileSearchRoot fallback = raw_root;
                fallback.path = absolute_root;
                if (fallback.label.empty()) {
                    fallback.label = RootLabelForPath(absolute_root);
                }
                if (current_root_path_.empty()) {
                    current_root_path_ = absolute_root;
                }
                AddDirectoryChoice(fallback, true, &seen_paths);
            }
        }
    }

    if (directories_.empty()) {
        std::error_code current_error;
        std::filesystem::path current =
            std::filesystem::absolute(std::filesystem::current_path(), current_error).lexically_normal();
        if (current_error || current.empty()) {
            current = std::filesystem::current_path();
        }
        current_root_path_ = current;
        directories_.push_back({
            FileSearchRoot{current, "."},
            true
        });
    }
}

void SearchFilesModalContent::AddDirectoryChoice(
    const FileSearchRoot& root,
    bool selected,
    std::vector<std::string>* seen_paths) {
    std::error_code error;
    const std::filesystem::path normalized =
        std::filesystem::absolute(root.path, error).lexically_normal();
    if (error) {
        return;
    }

    const std::string key = normalized.generic_string();
    if (seen_paths &&
        std::find(seen_paths->begin(), seen_paths->end(), key) != seen_paths->end()) {
        return;
    }

    if (seen_paths) {
        seen_paths->push_back(key);
    }

    FileSearchRoot stored = root;
    stored.path = normalized;
    if (stored.label.empty()) {
        stored.label = RootLabelForPath(normalized);
    }

    directories_.push_back({stored, selected});
}

void SearchFilesModalContent::RefreshDirectoryLabels() {
    directory_labels_.clear();
    directory_labels_.reserve(directories_.size());

    for (const DirectoryChoice& directory : directories_) {
        directory_labels_.push_back(
            std::string(directory.selected ? "[x] " : "[ ] ") +
            directory.root.label +
            " — " +
            ToDisplayPath(directory.root.path));
    }

    if (selected_directory_ < 0) {
        selected_directory_ = 0;
    }
    if (!directory_labels_.empty() &&
        selected_directory_ >= static_cast<int>(directory_labels_.size())) {
        selected_directory_ = static_cast<int>(directory_labels_.size() - 1);
    }
}

void SearchFilesModalContent::ToggleSelectedDirectory() {
    if (directories_.empty()) {
        return;
    }

    if (selected_directory_ < 0 ||
        selected_directory_ >= static_cast<int>(directories_.size())) {
        selected_directory_ = 0;
    }

    directories_[static_cast<size_t>(selected_directory_)].selected =
        !directories_[static_cast<size_t>(selected_directory_)].selected;

    RefreshDirectoryLabels();
    status_ = DirectorySummaryText();
}

void SearchFilesModalContent::SelectAllDirectories() {
    for (DirectoryChoice& directory : directories_) {
        directory.selected = true;
    }
    RefreshDirectoryLabels();
    status_ = DirectorySummaryText();
}

void SearchFilesModalContent::ClearDirectorySelection() {
    for (DirectoryChoice& directory : directories_) {
        directory.selected = false;
    }
    RefreshDirectoryLabels();
    status_ = DirectorySummaryText();
}

std::vector<FileSearchRoot> SearchFilesModalContent::SelectedRoots() const {
    std::vector<FileSearchRoot> roots;

    for (const DirectoryChoice& directory : directories_) {
        if (directory.selected) {
            roots.push_back(directory.root);
        }
    }

    return roots;
}
