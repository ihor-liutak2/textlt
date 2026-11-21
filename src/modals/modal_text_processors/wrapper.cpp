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
    view_ = ftxui::CatchEvent(modal_, [this](ftxui::Event event) {
        return content_->HandleQuickKey(std::move(event));
    });
    modal_->SetBodyFrameScrolling(false);
    SetProcessorFooter();
}

void TextProcessorsModal::SetProcessorFooter() {
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
        }, ButtonRole::Cancel},
        {"Pin", [this] {
            if (content_) {
                content_->TogglePinned();
            }
        }, ButtonRole::Cancel},
        {"Add to Quick Panel", [this] { content_->AddToQuickPanel(); }},
        {"Close", [this] { Close(); }},
    });
}

ftxui::Component TextProcessorsModal::View() const {
    return view_;
}

void TextProcessorsModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
        SetProcessorFooter();
        content_->GetMainComponent()->TakeFocus();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
    }
}

void TextProcessorsModal::OpenQuick(bool use_paragraph) {
    open_ = true;
    content_->OpenQuick(use_paragraph);
    modal_->SetFooterButtons({
        {"Remove", [this] { content_->PromptRemoveQuick(); }, ButtonRole::Danger},
        {"Close", [this] { Close(); }},
    }, 1, true);
    content_->GetMainComponent()->TakeFocus();
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

    if (content_->HandleQuickKey(event)) return true;
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
