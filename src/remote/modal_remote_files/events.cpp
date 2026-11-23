bool RemoteFilesModalContent::HandleScrollbarMouse(
    PanelSide side, ftxui::Event& event, PanelComponent& component) {
    using ftxui::Mouse;
    auto& panel = Panel(side);
    const auto& mouse = event.mouse();
    if (panel.scrollbar_drag && mouse.motion == Mouse::Released) {
        panel.scrollbar_drag.reset();
        return true;
    }

    const ScrollbarMetrics bar(BoxWidth(panel.scrollbar_box),
        panel.entries.empty() ? 0 : panel.widest_name, panel.name_scroll_offset);
    if (panel.bounds.Contain(mouse.x, mouse.y)) {
        const bool left = mouse.button == Mouse::WheelLeft ||
            (mouse.shift && mouse.button == Mouse::WheelUp);
        const bool right = mouse.button == Mouse::WheelRight ||
            (mouse.shift && mouse.button == Mouse::WheelDown);
        if (left || right) {
            panel.name_scroll_offset = std::clamp(
                panel.name_scroll_offset + (left ? -4 : 4), 0, bar.max_offset);
            return true;
        }
    }

    const int column = mouse.x - panel.scrollbar_box.x_min;
    if (mouse.motion == Mouse::Pressed && mouse.button == Mouse::Left &&
        panel.scrollbar_box.Contain(mouse.x, mouse.y)) {
        if (bar.max_offset == 0 || bar.Travel() == 0) {
            return true;
        }
        panel.scrollbar_drag = component.CaptureMouse(event);
        if (!panel.scrollbar_drag) {
            return false;
        }
        SelectPanel(side);
        panel.last_clicked_entry = -1;
        const int within_thumb = column - bar.thumb_start;
        if (within_thumb >= 0 && within_thumb < bar.thumb_length) {
            panel.scrollbar_grab_column = within_thumb;
            return true;
        }
        panel.scrollbar_grab_column = bar.thumb_length / 2;
    } else if (!panel.scrollbar_drag || mouse.motion != Mouse::Moved) {
        return false;
    }

    if (bar.Travel() > 0) {
        const int start = std::clamp(column - panel.scrollbar_grab_column, 0, bar.Travel());
        if (start != bar.thumb_start) {
            panel.name_scroll_offset = bar.OffsetForThumb(start);
        }
    }
    return true;
}

bool RemoteFilesModalContent::HandlePanelEvent(
    PanelSide side, ftxui::Event event, PanelComponent& component) {
    PanelState& panel = Panel(side);
    if (event.is_mouse() && HandleScrollbarMouse(side, event, component)) {
        return true;
    }
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

    if (event == ftxui::Event::ArrowLeftCtrl || event == ftxui::Event::ArrowRightCtrl) {
        const int max_offset = std::max(0, panel.widest_name - BoxWidth(panel.scrollbar_box));
        const int delta = event == ftxui::Event::ArrowLeftCtrl ? -4 : 4;
        panel.name_scroll_offset = std::clamp(panel.name_scroll_offset + delta, 0, max_offset);
        return true;
    }
    if (event == ftxui::Event::PageUp || event == ftxui::Event::PageDown) {
        const int page = std::max(1, panel.visible_rows);
        SelectEntry(side, panel.selected + (event == ftxui::Event::PageUp ? -page : page));
        return true;
    }

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
    const size_t end = std::min(panel.entries.size(), start + static_cast<size_t>(panel.visible_rows));
    for (size_t index = start; index < end; ++index) {
        if (panel.boxes[index].Contain(mouse.x, mouse.y)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}
