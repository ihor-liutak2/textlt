void RemoteFilesModalContent::CopyToRemote() {
    const RemoteEntry* entry = SelectedEntry(PanelSide::Local);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select a local file or folder to upload.", true);
        return;
    }
    if (!EnsureRemoteProvider()) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    const std::string remote_target = JoinRemotePath(remote_panel_.path, entry->name);
    if (PanelContainsName(PanelSide::Remote, entry->name)) {
        StartOverwriteConfirmation(PendingOperation::CopyToRemoteOverwrite, *entry, remote_target);
        return;
    }
    CopyToRemoteConfirmed(*entry, remote_target);
}

void RemoteFilesModalContent::CopyToRemoteConfirmed(
    const RemoteEntry& entry,
    const std::string& remote_target) {
    if (!EnsureRemoteProvider()) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    std::string error;
    bool success = false;
    if (entry.type == RemoteEntryType::Directory) {
        success = remote_provider_->UploadDirectory(entry.path, remote_target, error);
    } else if (entry.type == RemoteEntryType::File || entry.type == RemoteEntryType::Symlink) {
        success = remote_provider_->Upload(entry.path, remote_target, error);
    } else {
        SetStatus("Only files, symlinks, and directories can be uploaded.", true);
        return;
    }

    if (!success) {
        SetStatus(error.empty() ? "Upload failed." : error, true);
        return;
    }
    LoadPanel(PanelSide::Remote, remote_panel_.path);
    SetStatus((entry.type == RemoteEntryType::Directory ? "Uploaded folder: " : "Uploaded: ") + entry.name);
}

void RemoteFilesModalContent::CopyToLocal() {
    const RemoteEntry* entry = SelectedEntry(PanelSide::Remote);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select a remote file or folder to download.", true);
        return;
    }
    if (!remote_provider_) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    const std::string local_target = FileManager::PathToUtf8(
        FileManager::PathFromUtf8(local_panel_.path) / FileManager::PathFromUtf8(entry->name));
    std::error_code exists_error;
    const bool target_exists = std::filesystem::exists(FileManager::PathFromUtf8(local_target), exists_error);
    if (exists_error) {
        SetStatus("Cannot check local target: " + exists_error.message(), true);
        return;
    }
    if (target_exists) {
        StartOverwriteConfirmation(PendingOperation::CopyToLocalOverwrite, *entry, local_target);
        return;
    }
    CopyToLocalConfirmed(*entry, local_target);
}

void RemoteFilesModalContent::CopyToLocalConfirmed(
    const RemoteEntry& entry,
    const std::string& local_target) {
    if (!remote_provider_) {
        SetStatus("Remote provider is not available.", true);
        return;
    }

    std::string error;
    bool success = false;
    if (entry.type == RemoteEntryType::Directory) {
        success = remote_provider_->DownloadDirectory(entry.path, local_target, error);
    } else if (entry.type == RemoteEntryType::File || entry.type == RemoteEntryType::Symlink) {
        success = remote_provider_->Download(entry.path, local_target, error);
    } else {
        SetStatus("Only files, symlinks, and directories can be downloaded.", true);
        return;
    }

    if (!success) {
        SetStatus(error.empty() ? "Download failed." : error, true);
        return;
    }
    LoadPanel(PanelSide::Local, local_panel_.path);
    SetStatus((entry.type == RemoteEntryType::Directory ? "Downloaded folder: " : "Downloaded: ") + entry.name);
}

void RemoteFilesModalContent::StartOverwriteConfirmation(
    PendingOperation operation,
    const RemoteEntry& entry,
    const std::string& target_path) {
    pending_operation_ = operation;
    pending_panel_ = operation == PendingOperation::CopyToRemoteOverwrite
        ? PanelSide::Remote
        : PanelSide::Local;
    pending_copy_entry_ = entry;
    pending_copy_target_path_ = target_path;
    pending_input_label_ = "Type OVERWRITE";
    pending_input_value_.clear();
    pending_input_cursor_ = 0;
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Target already exists: " + TrimForDisplay(target_path, 80) + ". Type OVERWRITE and Confirm to replace it.", true);
}

void RemoteFilesModalContent::CopyActiveToOtherPanel() {
    if (active_panel_ == PanelSide::Local) {
        CopyToRemote();
        return;
    }
    CopyToLocal();
}

void RemoteFilesModalContent::OpenSelectedFile() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select a file to open.", true);
        return;
    }
    if (entry->type == RemoteEntryType::Directory) {
        OpenSelected(active_panel_);
        return;
    }

    std::filesystem::path local_path;
    const RemoteEntry* remote_entry_to_cache = nullptr;
    RemoteConnectionConfig remote_connection_to_cache;

    if (active_panel_ == PanelSide::Local) {
        local_path = FileManager::PathFromUtf8(entry->path);
    } else {
        if (!remote_provider_) {
            SetStatus("Remote provider is not available.", true);
            return;
        }
        if (connections_.empty() || selected_connection_ < 0 ||
            selected_connection_ >= static_cast<int>(connections_.size())) {
            SetStatus("Remote connection is not available.", true);
            return;
        }

        local_path = TempDownloadPath(entry->name);
        std::string error;
        if (!remote_provider_->Download(entry->path, FileManager::PathToUtf8(local_path), error)) {
            SetStatus(error.empty() ? "Download failed." : error, true);
            return;
        }
        remote_entry_to_cache = entry;
        remote_connection_to_cache = connections_[static_cast<size_t>(selected_connection_)];
    }

    std::string error;
    if (!open_local_file_ || !open_local_file_(local_path, error)) {
        SetStatus(error.empty() ? "Cannot open file." : error, true);
        return;
    }

    if (remote_entry_to_cache) {
        RememberCachedRemoteFile(local_path, *remote_entry_to_cache, remote_connection_to_cache);
        SetStatus("Opened remote file in local cache. Save keeps the local cached copy; press Sync Last to upload it manually.");
        return;
    }
    SetStatus("Opened: " + local_path.filename().string());
}

bool RemoteFilesModalContent::UploadCachedLocalFile(
    const std::filesystem::path& local_path,
    std::string& status,
    std::string& error) {
    status.clear();
    error.clear();

    const std::string key = LocalPathKey(local_path);
    auto iter = std::find_if(
        cached_remote_files_.begin(),
        cached_remote_files_.end(),
        [&key](const CachedRemoteFile& cached) {
            return LocalPathKey(cached.local_path) == key;
        });

    if (iter == cached_remote_files_.end()) {
        return false;
    }

    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(iter->local_path, exists_error)) {
        error = exists_error ? exists_error.message() : "Cached local file does not exist.";
        SetStatus(error, true);
        return false;
    }

    std::unique_ptr<IRemoteProvider> provider;
    if (iter->connection.type == RemoteConnectionType::Sftp) {
        provider = std::make_unique<RemoteSftpProvider>();
    } else if (iter->connection.type == RemoteConnectionType::Dropbox) {
        provider = std::make_unique<RemoteDropboxProvider>();
    } else if (iter->connection.type == RemoteConnectionType::GoogleDrive) {
        provider = std::make_unique<RemoteGoogleDriveProvider>();
    } else if (iter->connection.type == RemoteConnectionType::MicrosoftDrive) {
        provider = std::make_unique<RemoteMicrosoftDriveProvider>();
    } else {
        error = "Manual sync is implemented for SFTP, Dropbox, Google Drive, and Microsoft cached files only.";
        SetStatus(error, true);
        return false;
    }

    if (!provider->Connect(iter->connection, error)) {
        SetStatus(error.empty() ? "Cannot connect to remote." : error, true);
        return false;
    }

    if (!provider->Upload(FileManager::PathToUtf8(iter->local_path), iter->remote_path, error)) {
        SetStatus(error.empty() ? "Remote upload failed." : error, true);
        return false;
    }

    last_cached_remote_index_ = static_cast<int>(std::distance(cached_remote_files_.begin(), iter));
    status = "manual sync uploaded remote file: " + iter->remote_name;
    SetStatus(status);

    if (remote_provider_ && RemoteParentPath(iter->remote_path) == remote_panel_.path) {
        LoadPanel(PanelSide::Remote, remote_panel_.path);
    }
    return true;
}

void RemoteFilesModalContent::UploadLastOpenedRemoteFile() {
    if (last_cached_remote_index_ < 0 ||
        last_cached_remote_index_ >= static_cast<int>(cached_remote_files_.size())) {
        SetStatus("Open a remote file first, edit/save the local cached copy, then press Sync Last.", true);
        return;
    }

    const std::filesystem::path local_path =
        cached_remote_files_[static_cast<size_t>(last_cached_remote_index_)].local_path;
    std::string status;
    std::string error;
    if (!UploadCachedLocalFile(local_path, status, error)) {
        SetStatus(error.empty() ? "Nothing to sync." : error, true);
        return;
    }
    SetStatus(status);
}


void RemoteFilesModalContent::ClearCachedRemoteFiles() {
    int removed_count = 0;
    int kept_count = 0;
    std::string last_error;

    for (const CachedRemoteFile& cached : cached_remote_files_) {
        std::error_code exists_error;
        if (!std::filesystem::exists(cached.local_path, exists_error)) {
            continue;
        }

        std::error_code remove_error;
        if (std::filesystem::remove(cached.local_path, remove_error)) {
            ++removed_count;
            continue;
        }

        ++kept_count;
        if (last_error.empty()) {
            last_error = remove_error ? remove_error.message() : "file was not removed";
        }
    }

    cached_remote_files_.clear();
    last_cached_remote_index_ = -1;

    if (kept_count > 0) {
        SetStatus(
            "Cleared cache index, but " + std::to_string(kept_count) +
                " cached file(s) were not removed: " + last_error,
            true);
        return;
    }

    SetStatus("Cleared remote cache. Removed files: " + std::to_string(removed_count) + ".");
}

void RemoteFilesModalContent::CopySelectedPath() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    const std::string text = entry ? entry->path : Panel(active_panel_).path;
    if (text.empty()) {
        SetStatus("Nothing to copy.", true);
        return;
    }
    if (copy_text_) {
        copy_text_(text);
    }
    SetStatus("Copied path.");
}
