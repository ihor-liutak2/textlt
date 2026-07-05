RemoteFilesModal::RemoteFilesModal(
    const Theme* theme,
    RemoteConfigStore* config_store,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    OpenLocalFileCallback open_local_file,
    CopyTextCallback copy_text)
    : theme_(theme) {
    content_ = std::make_shared<RemoteFilesModalContent>(
        theme_,
        config_store,
        file_manager,
        std::move(start_directory_provider),
        std::move(open_local_file),
        std::move(copy_text),
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component RemoteFilesModal::View() const {
    return modal_;
}

void RemoteFilesModal::Open() {
    open_ = true;
    content_->Open();
    modal_->TakeFocus();
}

void RemoteFilesModal::Close() {
    open_ = false;
    content_->Close();
}

bool RemoteFilesModal::IsOpen() const {
    return open_;
}

bool RemoteFilesModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}
