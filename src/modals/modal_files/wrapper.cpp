FilesModal::FilesModal(
    const Theme* theme,
    FileManager* file_manager,
    StartDirectoryProvider start_directory_provider,
    FavoriteDirectoriesProvider favorite_directories_provider,
    AddFavoriteDirectoryCallback on_add_favorite_directory,
    CopyPathCallback on_copy_path,
    ConfirmPathCallback on_confirm_path)
    : theme_(theme) {
    content_ = std::make_shared<FilesModalContent>(
        theme_,
        file_manager,
        std::move(start_directory_provider),
        std::move(favorite_directories_provider),
        std::move(on_add_favorite_directory),
        std::move(on_copy_path),
        std::move(on_confirm_path),
        [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
    RebuildFooterButtons();
}

ftxui::Component FilesModal::View() const {
    return modal_;
}

void FilesModal::Open(
    FilesModalMode mode,
    const std::filesystem::path& start_path,
    std::string suggested_file_name) {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open(mode, start_path, std::move(suggested_file_name));
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
        RebuildFooterButtons();
        modal_->RefreshChildren();
    }
}

void FilesModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool FilesModal::IsOpen() const {
    return open_;
}

bool FilesModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }

    if (content_ && event.is_mouse()) {
        const bool modal_handled = modal_->OnEvent(event);
        if (content_->HandleEvent(std::move(event))) {
            return true;
        }
        return modal_handled;
    }

    if (content_ && content_->HandleEvent(event)) {
        return true;
    }

    return modal_->OnEvent(std::move(event));
}

void FilesModal::RebuildFooterButtons() {
    if (!modal_ || !content_) {
        return;
    }

    if (content_->Mode() == FilesModalMode::Manage || content_->Mode() == FilesModalMode::None) {
        modal_->SetFooterButtons({
            {"Close", [this] { Close(); }},
        });
        return;
    }

    modal_->SetFooterButtons({
        {content_->Mode() == FilesModalMode::SaveAs ? "Save As" :
            content_->Mode() == FilesModalMode::Import ? "Import" : "Open",
            [this] {
                if (content_) {
                    content_->ConfirmSelected();
                }
            }},
        {"Close", [this] { Close(); }},
    });
}
