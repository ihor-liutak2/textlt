RemoteEntry* RemoteFilesModalContent::SelectedEntry(PanelSide side) {
    PanelState& panel = Panel(side);
    if (panel.selected < 0 || panel.selected >= static_cast<int>(panel.entries.size())) {
        return nullptr;
    }
    return &panel.entries[static_cast<size_t>(panel.selected)];
}

const RemoteEntry* RemoteFilesModalContent::SelectedEntry(PanelSide side) const {
    const PanelState& panel = Panel(side);
    if (panel.selected < 0 || panel.selected >= static_cast<int>(panel.entries.size())) {
        return nullptr;
    }
    return &panel.entries[static_cast<size_t>(panel.selected)];
}

RemoteFilesModalContent::PanelState& RemoteFilesModalContent::Panel(PanelSide side) {
    return side == PanelSide::Local ? local_panel_ : remote_panel_;
}

const RemoteFilesModalContent::PanelState& RemoteFilesModalContent::Panel(PanelSide side) const {
    return side == PanelSide::Local ? local_panel_ : remote_panel_;
}

IRemoteProvider* RemoteFilesModalContent::Provider(PanelSide side) {
    return side == PanelSide::Local ? static_cast<IRemoteProvider*>(&local_provider_)
                                    : static_cast<IRemoteProvider*>(remote_provider_.get());
}

const IRemoteProvider* RemoteFilesModalContent::Provider(PanelSide side) const {
    return side == PanelSide::Local ? static_cast<const IRemoteProvider*>(&local_provider_)
                                    : static_cast<const IRemoteProvider*>(remote_provider_.get());
}

std::string RemoteFilesModalContent::EntryTypeLabel(RemoteEntryType type) {
    switch (type) {
        case RemoteEntryType::Directory:
            return "DIR";
        case RemoteEntryType::File:
            return "FILE";
        case RemoteEntryType::Symlink:
            return "LINK";
        case RemoteEntryType::Other:
            return "OTHER";
    }
    return "OTHER";
}

std::string RemoteFilesModalContent::EntrySizeLabel(const RemoteEntry& entry) {
    if (entry.type == RemoteEntryType::Directory) {
        return "";
    }
    return FileManager::FormatFileSize(entry.size);
}

std::string RemoteFilesModalContent::TrimForDisplay(const std::string& value, size_t width) {
    if (textlt::utils::Utf8DisplayWidth(value) <= width) {
        return value;
    }
    if (width <= 3) {
        return value.substr(
            0,
            textlt::utils::Utf8ByteIndexAtDisplayColumn(value, 0, width));
    }
    return value.substr(
        0,
        textlt::utils::Utf8ByteIndexAtDisplayColumn(value, 0, width - 3)) + "...";
}

bool RemoteFilesModalContent::IsParentEntry(const RemoteEntry& entry) {
    return entry.name == "..";
}

bool RemoteFilesModalContent::PanelContainsName(PanelSide side, const std::string& name) const {
    const PanelState& panel = Panel(side);
    return std::any_of(panel.entries.begin(), panel.entries.end(), [&name](const RemoteEntry& entry) {
        return !IsParentEntry(entry) && entry.name == name;
    });
}

std::string RemoteFilesModalContent::LocalPathKey(const std::filesystem::path& path) {
    std::error_code absolute_error;
    std::filesystem::path absolute_path = std::filesystem::absolute(path, absolute_error);
    if (absolute_error) {
        absolute_path = path;
    }
    return absolute_path.lexically_normal().string();
}

std::filesystem::path RemoteFilesModalContent::TempDownloadPath(const std::string& name) const {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    std::string safe_name = name.empty() ? "remote.txt" : name;
    for (char& ch : safe_name) {
        if (ch == '/' || ch == '\\') {
            ch = '_';
        }
    }
    return std::filesystem::temp_directory_path() /
        ("textlt-remote-" + std::to_string(ticks) + "-" + safe_name);
}

void RemoteFilesModalContent::RememberCachedRemoteFile(
    const std::filesystem::path& local_path,
    const RemoteEntry& remote_entry,
    const RemoteConnectionConfig& config) {
    const std::string key = LocalPathKey(local_path);
    auto iter = std::find_if(
        cached_remote_files_.begin(),
        cached_remote_files_.end(),
        [&key](const CachedRemoteFile& cached) {
            return LocalPathKey(cached.local_path) == key;
        });

    CachedRemoteFile cached;
    cached.local_path = local_path;
    cached.remote_path = remote_entry.path;
    cached.remote_name = remote_entry.name;
    cached.connection = config;

    if (iter == cached_remote_files_.end()) {
        cached_remote_files_.push_back(std::move(cached));
        last_cached_remote_index_ = static_cast<int>(cached_remote_files_.size()) - 1;
        return;
    }

    *iter = std::move(cached);
    last_cached_remote_index_ = static_cast<int>(std::distance(cached_remote_files_.begin(), iter));
}


std::string RemoteFilesModalContent::LastCachedLabel() const {
    if (last_cached_remote_index_ < 0 ||
        last_cached_remote_index_ >= static_cast<int>(cached_remote_files_.size())) {
        return "Cached remote file: none";
    }

    const CachedRemoteFile& cached = cached_remote_files_[static_cast<size_t>(last_cached_remote_index_)];
    const std::string name = cached.remote_name.empty() ? cached.remote_path : cached.remote_name;
    return "Cached remote file: " + TrimForDisplay(name, 28) +
        "  remote: " + TrimForDisplay(cached.remote_path, 54);
}

void RemoteFilesModalContent::SetStatus(std::string status, bool is_error) {
    status_ = std::move(status);
    status_is_error_ = is_error;
}

void RemoteFilesModalContent::SetPanelStatus(PanelSide side, std::string status, bool is_error) {
    PanelState& panel = Panel(side);
    panel.status = std::move(status);
    panel.status_is_error = is_error;
    if (is_error) {
        error_footer_ = panel.status;
    } else if (side == PanelSide::Remote) {
        error_footer_.clear();
    }
}
