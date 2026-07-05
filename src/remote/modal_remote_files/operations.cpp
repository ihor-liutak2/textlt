void RemoteFilesModalContent::StartRename() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select an item to rename.", true);
        return;
    }
    pending_operation_ = PendingOperation::Rename;
    pending_panel_ = active_panel_;
    pending_input_label_ = "New name";
    pending_input_value_ = entry->name;
    pending_input_cursor_ = static_cast<int>(pending_input_value_.size());
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Confirm rename.");
}

void RemoteFilesModalContent::StartMakeDirectory() {
    pending_operation_ = PendingOperation::MakeDirectory;
    pending_panel_ = active_panel_;
    pending_input_label_ = "Directory name";
    pending_input_value_ = "New Folder";
    pending_input_cursor_ = static_cast<int>(pending_input_value_.size());
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Confirm directory creation.");
}

void RemoteFilesModalContent::StartDelete() {
    const RemoteEntry* entry = SelectedEntry(active_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Select an item to delete.", true);
        return;
    }
    pending_operation_ = PendingOperation::Delete;
    pending_panel_ = active_panel_;
    pending_input_label_ = "Type DELETE";
    pending_input_value_.clear();
    pending_input_cursor_ = 0;
    pending_copy_target_path_ = entry->path;
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
    SetStatus("Delete " + TrimForDisplay(entry->path, 80) + "? Type DELETE and press Confirm.", true);
}

void RemoteFilesModalContent::ConfirmPendingOperation() {
    if (pending_operation_ == PendingOperation::None) {
        return;
    }

    IRemoteProvider* provider = Provider(pending_panel_);
    if (!provider) {
        SetStatus("Provider is not available.", true);
        return;
    }

    if (pending_operation_ == PendingOperation::CopyToRemoteOverwrite ||
        pending_operation_ == PendingOperation::CopyToLocalOverwrite) {
        if (pending_input_value_ != "OVERWRITE") {
            SetStatus("Type OVERWRITE to replace the existing target.", true);
            return;
        }

        const PendingOperation operation = pending_operation_;
        const RemoteEntry entry = pending_copy_entry_;
        const std::string target_path = pending_copy_target_path_;
        CancelPendingOperation();

        if (operation == PendingOperation::CopyToRemoteOverwrite) {
            CopyToRemoteConfirmed(entry, target_path);
            return;
        }
        CopyToLocalConfirmed(entry, target_path);
        return;
    }

    std::string error;
    if (pending_operation_ == PendingOperation::MakeDirectory) {
        if (!IsPlainRemoteName(pending_input_value_)) {
            SetStatus("Directory name must be plain name.", true);
            return;
        }
        const std::string target = pending_panel_ == PanelSide::Local
            ? FileManager::PathToUtf8(
                FileManager::PathFromUtf8(Panel(pending_panel_).path) /
                FileManager::PathFromUtf8(pending_input_value_))
            : JoinRemotePath(Panel(pending_panel_).path, pending_input_value_);
        if (!provider->MakeDirectory(target, error)) {
            SetStatus(error.empty() ? "Mkdir failed." : error, true);
            return;
        }
        LoadPanel(pending_panel_, Panel(pending_panel_).path);
        CancelPendingOperation();
        SetStatus("Created directory.");
        return;
    }

    RemoteEntry* entry = SelectedEntry(pending_panel_);
    if (!entry || IsParentEntry(*entry)) {
        SetStatus("Selected item changed.", true);
        return;
    }

    if (pending_operation_ == PendingOperation::Rename) {
        if (!IsPlainRemoteName(pending_input_value_)) {
            SetStatus("New name must be plain name.", true);
            return;
        }
        const std::string target = pending_panel_ == PanelSide::Local
            ? FileManager::PathToUtf8(
                FileManager::PathFromUtf8(entry->path).parent_path() /
                FileManager::PathFromUtf8(pending_input_value_))
            : JoinRemotePath(RemoteParentPath(entry->path), pending_input_value_);
        if (!provider->Rename(entry->path, target, error)) {
            SetStatus(error.empty() ? "Rename failed." : error, true);
            return;
        }
        LoadPanel(pending_panel_, Panel(pending_panel_).path);
        CancelPendingOperation();
        SetStatus("Renamed item.");
        return;
    }

    if (pending_operation_ == PendingOperation::Delete) {
        if (pending_input_value_ != "DELETE") {
            SetStatus("Type DELETE to confirm deletion.", true);
            return;
        }
        const bool success = entry->type == RemoteEntryType::Directory
            ? provider->RemoveDirectory(entry->path, error)
            : provider->RemoveFile(entry->path, error);
        if (!success) {
            SetStatus(error.empty() ? "Delete failed." : error, true);
            return;
        }
        LoadPanel(pending_panel_, Panel(pending_panel_).path);
        CancelPendingOperation();
        SetStatus("Deleted item.");
    }
}

void RemoteFilesModalContent::CancelPendingOperation() {
    pending_operation_ = PendingOperation::None;
    pending_input_label_.clear();
    pending_input_value_.clear();
    pending_input_cursor_ = 0;
    pending_copy_entry_ = RemoteEntry{};
    pending_copy_target_path_.clear();
}
