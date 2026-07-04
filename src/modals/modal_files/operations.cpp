void FilesModalContent::StartCreateDirectoryOperation() {
    StartNameOperation(
        PendingFileOperation::CreateDirectoryItem,
        "Directory name",
        "New Folder",
        "Create directory in current directory?");
}

void FilesModalContent::StartCreateFileOperation() {
    StartNameOperation(
        PendingFileOperation::CreateFileItem,
        "File name",
        "new_file.txt",
        "Create empty file in current directory?");
}

void FilesModalContent::StartDeleteOperation() {
    std::string error;
    const std::vector<std::filesystem::path> paths = SelectedOperationPaths(error);
    if (!error.empty()) {
        SetStatus(error, true);
        return;
    }
    StartConfirmOperation(
        PendingFileOperation::DeleteItems,
        "Delete " + std::to_string(paths.size()) + " selected item(s)?");
    pending_operation_paths_ = paths;
}

void FilesModalContent::StartRenameOperation() {
    std::string error;
    const std::vector<std::filesystem::path> paths = SelectedOperationPaths(error);
    if (!error.empty()) {
        SetStatus(error, true);
        return;
    }
    if (paths.size() != 1) {
        SetStatus("Rename works with exactly one selected item.", true);
        return;
    }

    StartNameOperation(
        PendingFileOperation::RenameItem,
        "New name",
        FileManager::PathToUtf8(paths.front().filename()),
        "Rename selected item?");
    pending_operation_paths_ = paths;
}

void FilesModalContent::StartCopyOperation() {
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }
    std::string error;
    const std::vector<std::filesystem::path> paths = SelectedOperationPaths(error);
    if (!error.empty()) {
        SetStatus(error, true);
        return;
    }
    if (!file_manager_->CopyItems(paths, error)) {
        SetStatus(error.empty() ? "Copy failed." : error, true);
        return;
    }
    CancelPendingOperation();
    SetStatus("Copied " + std::to_string(paths.size()) + " item(s) to file clipboard.");
}

void FilesModalContent::StartCutOperation() {
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }
    std::string error;
    const std::vector<std::filesystem::path> paths = SelectedOperationPaths(error);
    if (!error.empty()) {
        SetStatus(error, true);
        return;
    }
    if (!file_manager_->CutItems(paths, error)) {
        SetStatus(error.empty() ? "Cut failed." : error, true);
        return;
    }
    CancelPendingOperation();
    SetStatus("Cut " + std::to_string(paths.size()) + " item(s) to file clipboard.");
}

void FilesModalContent::StartPasteOperation() {
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }
    if (!file_manager_->CanPaste()) {
        SetStatus("File clipboard is empty.", true);
        return;
    }
    const size_t count = file_manager_->ClipboardSources().size();
    StartConfirmOperation(
        PendingFileOperation::PasteItems,
        "Paste " + std::to_string(count) + " item(s) into current directory?");
}

void FilesModalContent::StartNameOperation(
    PendingFileOperation operation,
    std::string label,
    std::string default_value,
    std::string message) {
    pending_operation_ = operation;
    pending_operation_input_label_ = std::move(label);
    pending_operation_input_value_ = std::move(default_value);
    pending_operation_input_cursor_ = static_cast<int>(pending_operation_input_value_.size());
    pending_operation_paths_.clear();
    pending_operation_message_ = std::move(message);
    operation_layer_index_ = 1;
    status_is_error_ = false;
    status_ = "Confirm operation or cancel.";
    if (operation_input_) {
        operation_input_->TakeFocus();
    }
}

void FilesModalContent::StartConfirmOperation(
    PendingFileOperation operation,
    std::string message) {
    pending_operation_ = operation;
    pending_operation_input_label_.clear();
    pending_operation_input_value_.clear();
    pending_operation_input_cursor_ = 0;
    pending_operation_paths_.clear();
    pending_operation_message_ = std::move(message);
    operation_layer_index_ = 1;
    status_is_error_ = false;
    status_ = "Confirm operation or cancel.";
    if (confirm_yes_button_) {
        confirm_yes_button_->TakeFocus();
    }
}

void FilesModalContent::CancelPendingOperation() {
    pending_operation_ = PendingFileOperation::None;
    operation_layer_index_ = 0;
    pending_operation_message_.clear();
    pending_operation_input_label_.clear();
    pending_operation_input_value_.clear();
    pending_operation_input_cursor_ = 0;
    pending_operation_paths_.clear();
    if (entry_list_component_) {
        entry_list_component_->TakeFocus();
    }
}

void FilesModalContent::ConfirmPendingOperation() {
    if (!HasPendingOperation()) {
        return;
    }
    if (!file_manager_) {
        SetStatus("File manager is not available.", true);
        return;
    }

    std::string error;
    switch (pending_operation_) {
        case PendingFileOperation::CreateDirectoryItem: {
            if (!FileManager::IsPlainName(pending_operation_input_value_)) {
                SetStatus("Enter only a directory name.", true);
                return;
            }
            const std::filesystem::path target = current_directory_ / FileManager::PathFromUtf8(pending_operation_input_value_);
            if (!file_manager_->CreateDirectoryItem(target, error)) {
                SetStatus(error.empty() ? "Create directory failed." : error, true);
                return;
            }
            CancelPendingOperation();
            Refresh();
            SetStatus("Directory created: " + FileManager::PathToUtf8(target.filename()));
            break;
        }
        case PendingFileOperation::CreateFileItem: {
            if (!FileManager::IsPlainName(pending_operation_input_value_)) {
                SetStatus("Enter only a file name.", true);
                return;
            }
            const std::filesystem::path target = current_directory_ / FileManager::PathFromUtf8(pending_operation_input_value_);
            if (!file_manager_->CreateEmptyFile(target, error)) {
                SetStatus(error.empty() ? "Create file failed." : error, true);
                return;
            }
            CancelPendingOperation();
            Refresh();
            SetStatus("File created: " + FileManager::PathToUtf8(target.filename()));
            break;
        }
        case PendingFileOperation::DeleteItems: {
            std::vector<std::filesystem::path> paths = pending_operation_paths_;
            if (paths.empty()) {
                paths = SelectedOperationPaths(error);
            }
            if (!error.empty()) {
                SetStatus(error, true);
                return;
            }
            if (!file_manager_->DeleteItems(paths, true, error)) {
                SetStatus(error.empty() ? "Delete failed." : error, true);
                return;
            }
            const size_t count = paths.size();
            CancelPendingOperation();
            Refresh();
            SetStatus("Deleted " + std::to_string(count) + " item(s).");
            break;
        }
        case PendingFileOperation::RenameItem: {
            std::vector<std::filesystem::path> paths = pending_operation_paths_;
            if (paths.empty()) {
                paths = SelectedOperationPaths(error);
            }
            if (!error.empty()) {
                SetStatus(error, true);
                return;
            }
            if (paths.size() != 1) {
                SetStatus("Rename works with exactly one selected item.", true);
                return;
            }
            std::filesystem::path destination;
            if (!file_manager_->RenameItem(
                    paths.front(),
                    pending_operation_input_value_,
                    destination,
                    error)) {
                SetStatus(error.empty() ? "Rename failed." : error, true);
                return;
            }
            CancelPendingOperation();
            Refresh();
            SetStatus("Renamed to: " + FileManager::PathToUtf8(destination.filename()));
            break;
        }
        case PendingFileOperation::PasteItems: {
            std::vector<std::filesystem::path> pasted_paths;
            if (!file_manager_->PasteItems(current_directory_, pasted_paths, error)) {
                SetStatus(error.empty() ? "Paste failed." : error, true);
                return;
            }
            const size_t count = pasted_paths.size();
            CancelPendingOperation();
            Refresh();
            SetStatus("Pasted " + std::to_string(count) + " item(s).");
            break;
        }
        case PendingFileOperation::None:
            break;
    }
}

bool FilesModalContent::HasPendingOperation() const {
    return pending_operation_ != PendingFileOperation::None;
}

bool FilesModalContent::PendingOperationNeedsInput() const {
    return pending_operation_ == PendingFileOperation::CreateDirectoryItem ||
        pending_operation_ == PendingFileOperation::CreateFileItem ||
        pending_operation_ == PendingFileOperation::RenameItem;
}

std::string FilesModalContent::PendingOperationActionLabel() const {
    switch (pending_operation_) {
        case PendingFileOperation::CreateDirectoryItem:
            return "Create Dir";
        case PendingFileOperation::CreateFileItem:
            return "Create File";
        case PendingFileOperation::DeleteItems:
            return "Delete";
        case PendingFileOperation::RenameItem:
            return "Rename";
        case PendingFileOperation::PasteItems:
            return "Paste";
        case PendingFileOperation::None:
            break;
    }
    return "Confirm";
}
