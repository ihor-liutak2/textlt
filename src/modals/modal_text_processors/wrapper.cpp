TextProcessorsModal::TextProcessorsModal(
    const Theme* theme,
    TargetTextProvider target_text_provider,
    ReplaceTargetCallback replace_target_text)
    : theme_(theme) {
    content_ = std::make_shared<TextProcessorsModalContent>(
        theme_,
        std::move(target_text_provider),
        std::move(replace_target_text),
        [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterButtons({
        {"Apply", [this] {
            if (content_) {
                content_->ApplySelected();
            }
        }},
        {"Reload", [this] {
            if (content_) {
                content_->Reload();
            }
        }},
        {"Pin", [this] {
            if (content_) {
                content_->TogglePinned();
            }
        }},
        {"Close", [this] { Close(); }},
    });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component TextProcessorsModal::View() const {
    return modal_;
}

void TextProcessorsModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
    }
}

void TextProcessorsModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool TextProcessorsModal::IsOpen() const {
    return open_;
}

bool TextProcessorsModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }

    if (event == ftxui::Event::Escape) {
        Close();
        return true;
    }

    if (content_ && content_->HandleEvent(event)) {
        return true;
    }

    return modal_->OnEvent(std::move(event));
}

} // namespace textlt
