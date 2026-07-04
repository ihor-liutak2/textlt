bool FilesModalContent::HandleEvent(ftxui::Event event) {
    if (operation_input_ && operation_input_->Focused()) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            ConfirmPendingOperation();
            return true;
        }
        if (IsEscapeEvent(event)) {
            CancelPendingOperation();
            SetStatus("Operation canceled.");
            return true;
        }
        return false;
    }

    if (path_input_ && path_input_->Focused()) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            LoadPathFromInput();
            return true;
        }
        return false;
    }

    if (file_name_input_ && file_name_input_->Focused()) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            ConfirmSelected();
            return true;
        }
        return false;
    }

    if (HasPendingOperation()) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            ConfirmPendingOperation();
            return true;
        }
        if (IsEscapeEvent(event)) {
            CancelPendingOperation();
            SetStatus("Operation canceled.");
            return true;
        }
        // Confirmation is modal: do not allow navigation or file actions
        // behind the popup until it is confirmed or cancelled.
        return true;
    }

    if (IsShiftArrowDownEvent(event)) {
        MoveSelectionWithRange(1);
        return true;
    }
    if (IsShiftArrowUpEvent(event)) {
        MoveSelectionWithRange(-1);
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        MoveSelection(1);
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        MoveSelection(-1);
        return true;
    }
    if (event == ftxui::Event::PageDown) {
        MoveSelection(kVisibleEntryRows);
        return true;
    }
    if (event == ftxui::Event::PageUp) {
        MoveSelection(-kVisibleEntryRows);
        return true;
    }
    if (event.input() == " ") {
        ToggleEntrySelection(selected_entry_);
        return true;
    }
    if (event == ftxui::Event::Return || event.input() == "\x0A") {
        ActivateSelected(false);
        return true;
    }
    if (IsBackspaceEvent(event)) {
        NavigateUp();
        return true;
    }
    if (event.input() == "r" || event.input() == "R") {
        Refresh();
        return true;
    }

    if (HandleFavoriteMouseEvent(event)) {
        return true;
    }
    return HandleEntryMouseEvent(event);
}

bool FilesModalContent::HandleEntryMouseEvent(ftxui::Event event) {
    if (!event.is_mouse()) {
        return false;
    }

    const ftxui::Mouse& mouse = event.mouse();
    if (mouse.button == ftxui::Mouse::WheelDown) {
        MoveSelection(3);
        return true;
    }
    if (mouse.button == ftxui::Mouse::WheelUp) {
        MoveSelection(-3);
        return true;
    }

    if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed) {
        return false;
    }

    for (size_t visible_index = 0; visible_index < entry_boxes_.size(); ++visible_index) {
        if (!entry_boxes_[visible_index].Contain(mouse.x, mouse.y)) {
            continue;
        }

        const int entry_index = scroll_offset_ + static_cast<int>(visible_index);
        if (entry_index < 0 || entry_index >= static_cast<int>(entries_.size())) {
            return false;
        }

        if (mouse.shift) {
            SelectRangeTo(entry_index);
            last_clicked_entry_ = -1;
            return true;
        }
        if (mouse.control) {
            ToggleEntrySelection(entry_index);
            last_clicked_entry_ = -1;
            return true;
        }

        ClearSelectionMarks();
        SelectEntry(entry_index);
        selection_anchor_ = entry_index;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_entry_click_time_).count();
        const bool is_double_click =
            last_clicked_entry_ == entry_index &&
            elapsed_ms >= kMinDoubleClickMs &&
            elapsed_ms <= kMaxDoubleClickMs;

        last_entry_click_time_ = now;
        last_clicked_entry_ = entry_index;

        if (is_double_click) {
            ActivateSelected(true);
            last_clicked_entry_ = -1;
        }
        return true;
    }
    return false;
}

bool FilesModalContent::HandleFavoriteMouseEvent(ftxui::Event event) {
    if (!event.is_mouse()) {
        return false;
    }
    const ftxui::Mouse& mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed) {
        return false;
    }

    for (size_t index = 0; index < favorite_boxes_.size(); ++index) {
        if (favorite_boxes_[index].Contain(mouse.x, mouse.y)) {
            if (index < favorite_directories_.size()) {
                last_clicked_entry_ = -1;
                LoadDirectory(favorite_directories_[index]);
                return true;
            }
        }
    }
    return false;
}

bool FilesModalContent::IsBackspaceEvent(const ftxui::Event& event) const {
    const std::string input = event.input();
    return event == ftxui::Event::Backspace || input == "\x7f" || input == "\b";
}

bool FilesModalContent::IsEscapeEvent(const ftxui::Event& event) const {
    return event.input() == "\x1B";
}

bool FilesModalContent::IsShiftArrowUpEvent(const ftxui::Event& event) const {
    const std::string input = event.input();
    return input == "\x1B[1;2A" || input == "\x1B[1;6A";
}

bool FilesModalContent::IsShiftArrowDownEvent(const ftxui::Event& event) const {
    const std::string input = event.input();
    return input == "\x1B[1;2B" || input == "\x1B[1;6B";
}
