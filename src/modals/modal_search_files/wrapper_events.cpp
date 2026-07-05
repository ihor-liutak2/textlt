SearchFilesModal::SearchFilesModal(
    const Theme* theme,
    RootProvider root_provider,
    OpenMatchCallback on_open,
    SearchFilesModalContent::ReadClipboardCallback read_clipboard,
    SearchFilesModalContent::WriteClipboardCallback write_clipboard)
    : theme_(theme) {
        content_ = std::make_shared<SearchFilesModalContent>(
            theme_,
            std::move(root_provider),
            std::move(on_open),
            std::move(read_clipboard),
            std::move(write_clipboard),
            [this] { Close(); });

    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });

    modal_->SetFooterButtons({
        {"Search", [this] {
            if (content_) {
                content_->ExecuteSearchFromFooter();
            }
        }, ButtonRole::Cancel},
        {"Close", [this] { Close(); }},
    });

    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component SearchFilesModal::View() const {
    return modal_;
}

void SearchFilesModal::Open() {
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

void SearchFilesModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool SearchFilesModal::IsOpen() const {
    return open_;
}

bool SearchFilesModal::OnEvent(ftxui::Event event) {
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


bool SearchFilesModalContent::HandleEvent(ftxui::Event event) {
    if (selected_tab_ == 0) {
        const std::string input = event.input();
        if (query_input_ && query_input_->Focused() &&
            (input == "Ctrl+V" || input == "Ctrl+Shift+V" ||
             event == ftxui::Event::Special("Ctrl+V") ||
             event == ftxui::Event::Special("Ctrl+Shift+V"))) {
            PasteSearchQuery();
            return true;
        }

        if (event.is_mouse() && query_input_ && query_input_->Focused()) {
            const ftxui::Mouse& mouse = event.mouse();
            if (mouse.button == ftxui::Mouse::Left &&
                mouse.motion == ftxui::Mouse::Pressed) {
                const auto now = std::chrono::steady_clock::now();
                const auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_query_click_time_).count();
                last_query_click_time_ = now;

                if (elapsed_ms >= 0 && elapsed_ms <= 500) {
                    query_input_->OnEvent(ftxui::Event::Special("Ctrl+A"));
                    return true;
                }
            }
        }

        return HandleDirectoryMouseEvent(event);
    }

    if (selected_tab_ != 1) {
        return false;
    }

    if (event == ftxui::Event::ArrowDown) {
        MoveResultSelection(1);
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        MoveResultSelection(-1);
        return true;
    }

    if (event == ftxui::Event::PageDown) {
        MoveResultSelection(10);
        return true;
    }

    if (event == ftxui::Event::PageUp) {
        MoveResultSelection(-10);
        return true;
    }

    if (event == ftxui::Event::Return) {
        OpenSelectedMatch();
        return true;
    }

    return HandleResultsMouseEvent(event);
}

bool SearchFilesModalContent::HandleResultsMouseEvent(ftxui::Event event) {
    if (!event.is_mouse() || selected_tab_ != 1) {
        return false;
    }

    const ftxui::Mouse& mouse = event.mouse();

    if (mouse.button == ftxui::Mouse::WheelDown) {
        MoveResultSelection(3);
        return true;
    }

    if (mouse.button == ftxui::Mouse::WheelUp) {
        MoveResultSelection(-3);
        return true;
    }

    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) {
        return false;
        }

        const int clicked_result = selected_result_;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_result_click_time_).count();

        const bool is_double_click =
        last_clicked_result_ == clicked_result &&
        elapsed_ms >= 0 &&
        elapsed_ms <= 500;

        last_result_click_time_ = now;
        last_clicked_result_ = clicked_result;

        if (is_double_click) {
            OpenSelectedMatch();
            return true;
        }

        return false;
}

bool SearchFilesModalContent::HandleDirectoryMouseEvent(ftxui::Event event) {
    if (!event.is_mouse() || selected_tab_ != 0) {
        return false;
    }

    if (!directory_menu_ || !directory_menu_->Focused()) {
        return false;
    }

    const ftxui::Mouse& mouse = event.mouse();

    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) {
        return false;
        }

        if (directories_.empty()) {
            return false;
        }

        if (selected_directory_ < 0) {
            selected_directory_ = 0;
        }

        if (selected_directory_ >= static_cast<int>(directories_.size())) {
            selected_directory_ = static_cast<int>(directories_.size() - 1);
        }

        const int clicked_directory = selected_directory_;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_directory_click_time_).count();

            const bool is_double_click =
            last_clicked_directory_ == clicked_directory &&
            elapsed_ms >= 0 &&
            elapsed_ms <= 500;

            last_directory_click_time_ = now;
            last_clicked_directory_ = clicked_directory;

            if (is_double_click) {
                ToggleSelectedDirectory();
                last_clicked_directory_ = -1;
                return true;
            }

            return false;
}
