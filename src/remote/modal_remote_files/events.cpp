bool RemoteFilesModalContent::HandlePanelEvent(PanelSide side, ftxui::Event event) {
    PanelState& panel = Panel(side);
    if (event.is_mouse() && !panel.bounds.Contain(event.mouse().x, event.mouse().y)) {
        return false;
    }
    if (event.is_mouse() && event.mouse().button != ftxui::Mouse::WheelUp &&
        event.mouse().button != ftxui::Mouse::WheelDown &&
        !(event.mouse().button == ftxui::Mouse::Left && event.mouse().motion == ftxui::Mouse::Pressed)) {
        return true;
    }
    if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
        SelectPanel(event == ftxui::Event::ArrowLeft ? PanelSide::Local : PanelSide::Remote);
        return true;
    }
    SelectPanel(side);

    if (event == ftxui::Event::ArrowDown) {
        SelectEntry(side, panel.selected + 1);
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        SelectEntry(side, panel.selected - 1);
        return true;
    }
    if (event == ftxui::Event::Return) {
        OpenSelected(side);
        return true;
    }
    if (IsBackspaceEvent(event)) {
        GoParent(side);
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::WheelDown) {
        SelectEntry(side, panel.selected + 3);
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::WheelUp) {
        SelectEntry(side, panel.selected - 3);
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
        const int index = EntryIndexAtMouse(panel, event.mouse());
        if (index >= 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - panel.last_click_time).count();
            SelectEntry(side, index);
            if (duration >= kDoubleClickMinMs && duration <= kDoubleClickMaxMs &&
                panel.last_clicked_entry == index) {
                panel.last_clicked_entry = -1;
                panel.last_click_time = {};
                OpenSelected(side);
                return true;
            }
            panel.last_clicked_entry = index;
            panel.last_click_time = now;
            return true;
        }
    }
    return false;
}

bool RemoteFilesModalContent::HandleGlobalContentEvent(ftxui::Event event) {
    if (event == ftxui::Event::F5) {
        CopyActiveToOtherPanel();
        return true;
    }
    if (event == ftxui::Event::F6) {
        StartRename();
        return true;
    }
    if (event == ftxui::Event::F7) {
        StartMakeDirectory();
        return true;
    }
    if (event == ftxui::Event::Delete || event == ftxui::Event::F8) {
        StartDelete();
        return true;
    }
    if (event == ftxui::Event::Escape && pending_operation_ != PendingOperation::None) {
        CancelPendingOperation();
        return true;
    }
    return false;
}

int RemoteFilesModalContent::EntryIndexAtMouse(
    const PanelState& panel,
    const ftxui::Mouse& mouse) const {
    const size_t start = static_cast<size_t>(panel.scroll_offset);
    const size_t end = std::min(panel.entries.size(), start + kVisibleRows);
    for (size_t index = start; index < end; ++index) {
        if (panel.boxes[index].Contain(mouse.x, mouse.y)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}
