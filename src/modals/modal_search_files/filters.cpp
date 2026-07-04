std::filesystem::path SearchFilesModalContent::CurrentRootPath() const {
    if (!current_root_path_.empty()) {
        return current_root_path_.lexically_normal();
    }

    if (root_provider_) {
        const std::vector<FileSearchRoot> roots = root_provider_();
        for (const FileSearchRoot& root : roots) {
            std::error_code error;
            std::filesystem::path normalized =
                std::filesystem::absolute(root.path, error).lexically_normal();
            if (!error && !normalized.empty()) {
                return normalized;
            }
        }
    }

    std::error_code error;
    std::filesystem::path current =
        std::filesystem::absolute(std::filesystem::current_path(), error).lexically_normal();
    return error ? std::filesystem::current_path() : current;
}

void SearchFilesModalContent::SelectMaskByValue(const std::string& value) {
    if (value.empty()) {
        return;
    }

    masks_ = value;
    for (size_t index = 0; index < mask_sets_.size(); ++index) {
        if (mask_sets_[index].value == value) {
            selected_mask_set_ = index;
            selected_filter_ = static_cast<int>(index);
            SyncFilterInputsFromSelection();
            return;
        }
    }
}

void SearchFilesModalContent::RestoreSelectedDirectoriesForCurrentRoot() {
    const std::filesystem::path root_path = CurrentRootPath();
    const std::string root_key = root_path.generic_string();
    if (root_key.empty()) {
        return;
    }

    Json config = LoadFilterConfig();
    auto selected_dirs = config.find("selected_dirs");
    if (selected_dirs == config.end() || !selected_dirs->is_array()) {
        return;
    }

    Json* matching_entry = nullptr;
    for (Json& entry : *selected_dirs) {
        if (!entry.is_object()) {
            continue;
        }
        if (JsonString(entry, "root") == root_key) {
            matching_entry = &entry;
            break;
        }
    }

    if (!matching_entry) {
        return;
    }

    const std::string active_mask = JsonString(*matching_entry, "active_mask");
    SelectMaskByValue(active_mask);

    std::vector<std::filesystem::path> selected_paths;
    auto directories = matching_entry->find("directories");
    if (directories != matching_entry->end() && directories->is_array()) {
        for (const Json& item : *directories) {
            if (!item.is_string()) {
                continue;
            }

            const std::string relative_string = item.get<std::string>();
            if (relative_string.empty()) {
                continue;
            }

            std::filesystem::path relative_path(relative_string);
            if (relative_path.is_absolute()) {
                continue;
            }

            std::filesystem::path absolute_path =
                (root_path / relative_path).lexically_normal();
            std::error_code status_error;
            if (!std::filesystem::is_directory(absolute_path, status_error)) {
                continue;
            }

            selected_paths.push_back(absolute_path);
        }
    }

    for (DirectoryChoice& directory : directories_) {
        directory.selected = false;
    }

    std::vector<std::string> seen_paths;
    for (const std::filesystem::path& selected_path : selected_paths) {
        const std::string selected_key = selected_path.generic_string();
        bool found = false;
        for (DirectoryChoice& directory : directories_) {
            if (directory.root.path.lexically_normal().generic_string() == selected_key) {
                directory.selected = true;
                found = true;
                break;
            }
        }

        if (!found) {
            const std::filesystem::path relative = selected_path.lexically_relative(root_path);
            FileSearchRoot extra_root;
            extra_root.path = selected_path;
            extra_root.label = relative.empty()
                ? RootLabelForPath(selected_path)
                : relative.generic_string();
            AddDirectoryChoice(extra_root, true, &seen_paths);
        }
    }

    Json cleaned_directories = Json::array();
    for (size_t index = 0; index < selected_paths.size() && index < 100; ++index) {
        const std::filesystem::path relative =
            selected_paths[index].lexically_relative(root_path);
        if (!relative.empty()) {
            cleaned_directories.push_back(relative.generic_string());
        }
    }

    const bool removed_missing =
        directories != matching_entry->end() &&
        directories->is_array() &&
        cleaned_directories.size() != directories->size();

    (*matching_entry)["directories"] = cleaned_directories;
    (*matching_entry)["active_mask"] = masks_;

    if (removed_missing) {
        SaveFilterConfig(config);
    }
}

void SearchFilesModalContent::SaveSelectedDirectoriesForCurrentRoot() {
    const std::filesystem::path root_path = CurrentRootPath();
    const std::string root_key = root_path.generic_string();
    if (root_key.empty()) {
        return;
    }

    Json config = LoadFilterConfig();
    if (!config.is_object()) {
        config = Json::object();
    }

    Json existing_entries = Json::array();
    auto existing = config.find("selected_dirs");
    if (existing != config.end() && existing->is_array()) {
        existing_entries = *existing;
    }

    Json new_entry = Json::object();
    new_entry["root"] = root_key;
    new_entry["active_mask"] = masks_;
    new_entry["directories"] = Json::array();

    size_t stored_directories = 0;
    for (const DirectoryChoice& directory : directories_) {
        if (!directory.selected || stored_directories >= 100) {
            continue;
        }

        std::error_code status_error;
        if (!std::filesystem::is_directory(directory.root.path, status_error)) {
            continue;
        }

        const std::filesystem::path relative =
            directory.root.path.lexically_normal().lexically_relative(root_path);
        if (relative.empty()) {
            continue;
        }

        auto iter = relative.begin();
        if (iter != relative.end() && *iter == "..") {
            continue;
        }

        new_entry["directories"].push_back(relative.generic_string());
        ++stored_directories;
    }

    Json selected_dirs = Json::array();
    selected_dirs.push_back(new_entry);

    for (const Json& entry : existing_entries) {
        if (!entry.is_object()) {
            continue;
        }
        if (JsonString(entry, "root") == root_key) {
            continue;
        }
        if (selected_dirs.size() >= 100) {
            break;
        }
        selected_dirs.push_back(entry);
    }

    config["selected_dirs"] = std::move(selected_dirs);

    if (!SaveFilterConfig(config)) {
        status_ = "Unable to save selected directories.";
    }
}

std::filesystem::path SearchFilesModalContent::FilterConfigPath() const {
    #ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && std::string(app_data).size() > 0) {
        return std::filesystem::path(app_data) / "textlt" / "search_file_filter.json";
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && std::string(user_profile).size() > 0) {
        return std::filesystem::path(user_profile) /
        "AppData" / "Roaming" / "textlt" / "search_file_filter.json";
    }

    return std::filesystem::path("search_file_filter.json");
    #else
    const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config_home && std::string(xdg_config_home).size() > 0) {
        return std::filesystem::path(xdg_config_home) /
        "textlt" / "search_file_filter.json";
    }

    const char* home = std::getenv("HOME");
    if (home && std::string(home).size() > 0) {
        return std::filesystem::path(home) /
        ".config" / "textlt" / "search_file_filter.json";
    }

    return std::filesystem::path("search_file_filter.json");
    #endif
}

Json SearchFilesModalContent::LoadFilterConfig() const {
    return LoadJsonObject(FilterConfigPath());
}

bool SearchFilesModalContent::SaveFilterConfig(const Json& root) const {
    return WriteJsonAtomically(FilterConfigPath(), root);
}

void SearchFilesModalContent::LoadFilters() {
    std::vector<FileSearchMaskSet> loaded;

    const Json root = LoadFilterConfig();
    const auto filters = root.find("filters");
    if (filters != root.end() && filters->is_array()) {
        for (const Json& item : *filters) {
            if (!item.is_object()) {
                continue;
            }

            const std::string name = JsonString(item, "name");
            const std::string value = JsonString(item, "value");

            if (!name.empty() && !value.empty()) {
                loaded.push_back({name, value});
            }
        }
    }

    if (loaded.empty()) {
        loaded = FileSearchEngine::DefaultMaskSets();
    }

    if (loaded.empty()) {
        loaded.push_back(FileSearchEngine::DefaultCodeMaskSet());
    }

    mask_sets_ = std::move(loaded);

    if (selected_mask_set_ >= mask_sets_.size()) {
        selected_mask_set_ = 0;
    }
    selected_filter_ = static_cast<int>(selected_mask_set_);
}

void SearchFilesModalContent::SaveFilters() {
    UpdateSelectedFilterFromInputs();

    const std::filesystem::path path = FilterConfigPath();

    Json root = LoadFilterConfig();
    root["filters"] = Json::array();

    for (const FileSearchMaskSet& filter : mask_sets_) {
        Json item = Json::object();
        item["name"] = filter.name;
        item["value"] = filter.value;
        root["filters"].push_back(item);
    }

    if (SaveFilterConfig(root)) {
        status_ = "Filters saved: " + path.string();
    } else {
        status_ = "Unable to save filters: " + path.string();
    }

    RebuildFilterLabels();
}

void SearchFilesModalContent::SaveFiltersFromFooter() {
    SaveFilters();
}

void SearchFilesModalContent::RebuildFilterLabels() {
    filter_labels_.clear();
    filter_labels_.reserve(mask_sets_.size());

    for (const FileSearchMaskSet& filter : mask_sets_) {
        filter_labels_.push_back(
            filter.name + " — " + TrimForDisplay(filter.value, 34));
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (!filter_labels_.empty() &&
        selected_filter_ >= static_cast<int>(filter_labels_.size())) {
        selected_filter_ = static_cast<int>(filter_labels_.size() - 1);
        }
}

void SearchFilesModalContent::SyncFilterInputsFromSelection() {
    if (mask_sets_.empty()) {
        filter_name_input_.clear();
        filter_value_input_.clear();
        selected_mask_set_ = 0;
        selected_filter_ = 0;
        return;
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    selected_mask_set_ = static_cast<size_t>(selected_filter_);

    filter_name_input_ = mask_sets_[selected_mask_set_].name;
    filter_value_input_ = mask_sets_[selected_mask_set_].value;
}

void SearchFilesModalContent::UpdateSelectedFilterFromInputs() {
    if (mask_sets_.empty()) {
        return;
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    selected_mask_set_ = static_cast<size_t>(selected_filter_);

    if (filter_name_input_.empty()) {
        filter_name_input_ = "New Filter";
    }

    if (filter_value_input_.empty()) {
        filter_value_input_ = "*";
    }

    mask_sets_[selected_mask_set_].name = filter_name_input_;
    mask_sets_[selected_mask_set_].value = filter_value_input_;

    RebuildFilterLabels();
}

void SearchFilesModalContent::ApplySelectedFilter() {
    UpdateSelectedFilterFromInputs();

    if (mask_sets_.empty()) {
        status_ = "No filters available.";
        return;
    }

    UseMaskSet(static_cast<size_t>(selected_filter_));
    selected_tab_ = 0;
    status_ = "Applied filter: " + mask_sets_[selected_mask_set_].name;

    if (query_input_) {
        query_input_->TakeFocus();
    }
}

void SearchFilesModalContent::AddFilter() {
    UpdateSelectedFilterFromInputs();

    FileSearchMaskSet filter;
    filter.name = filter_name_input_.empty() ? "New Filter" : filter_name_input_;
    filter.value = filter_value_input_.empty() ? masks_ : filter_value_input_;

    if (filter.value.empty()) {
        filter.value = "*";
    }

    mask_sets_.push_back(filter);

    selected_mask_set_ = mask_sets_.size() - 1;
    selected_filter_ = static_cast<int>(selected_mask_set_);

    RebuildFilterLabels();
    SyncFilterInputsFromSelection();

    status_ = "Filter added.";
}

void SearchFilesModalContent::DeleteFilter() {
    if (mask_sets_.size() <= 1) {
        status_ = "At least one filter is required.";
        return;
    }

    if (selected_filter_ < 0) {
        selected_filter_ = 0;
    }

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    mask_sets_.erase(mask_sets_.begin() + selected_filter_);

    if (selected_filter_ >= static_cast<int>(mask_sets_.size())) {
        selected_filter_ = static_cast<int>(mask_sets_.size() - 1);
    }

    selected_mask_set_ = static_cast<size_t>(selected_filter_);

    RebuildFilterLabels();
    SyncFilterInputsFromSelection();

    status_ = "Filter deleted.";
}

ftxui::Element SearchFilesModalContent::RenderFiltersTab() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return ftxui::vbox({
        ftxui::text(""),
                       ftxui::hbox({
                           ftxui::text("Filters:") |
                           ftxui::bold |
                           ftxui::color(theme.modal_accent),
                                   ftxui::filler(),
                                   apply_filter_button_->Render(),
                                   ftxui::text(" "),
                                   add_filter_button_->Render(),
                                   ftxui::text(" "),
                                   delete_filter_button_->Render(),
                                   ftxui::text(" "),
                                   save_filters_button_->Render(),
                       }),
                       ftxui::separator() | ftxui::color(theme.modal_border),
                       ftxui::hbox({
                           filter_menu_->Render() |
                           ftxui::vscroll_indicator |
                           ftxui::frame |
                           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 45) |
                           ftxui::yflex,

                           ftxui::separator() | ftxui::color(theme.modal_border),

                                   ftxui::vbox({
                                       ftxui::text("Name:") | ftxui::color(theme.modal_text_color),
                                               filter_name_input_component_->Render() |
                                               ftxui::bgcolor(theme.modal_input_bg) |
                                               ftxui::color(theme.modal_input_fg),

                                               ftxui::text(""),
                                               ftxui::text("Masks:") | ftxui::color(theme.modal_text_color),
                                               filter_value_input_component_->Render() |
                                               ftxui::bgcolor(theme.modal_input_bg) |
                                               ftxui::color(theme.modal_input_fg),

                                               ftxui::text(""),
                                               ftxui::paragraph(
                                                   "Apply copies the selected filter into the Search tab. "
                                                   "Save writes search_file_filter.json.") |
                                                   ftxui::color(theme.modal_text_color) |
                                                   ftxui::dim,
                                   }) | ftxui::xflex,
                       }) | ftxui::yflex,
    });
}
