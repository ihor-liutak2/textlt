#include "context_menu_component.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt {

ContextMenuComponent::ContextMenuComponent(const Theme& theme, Execute execute)
    : theme_(theme), execute_(std::move(execute)) {}

void ContextMenuComponent::Open(int x, int y, std::vector<Item> items) {
    Close();
    anchor_x_ = x;
    anchor_y_ = y;
    items_ = std::move(items);
    MoveSelection(1);
}

void ContextMenuComponent::Close() {
    items_.clear();
    selected_ = -1;
    pressed_ = -1;
    first_row_ = 0;
    box_ = {0, -1, 0, -1};
}

bool ContextMenuComponent::IsEnabled(int index) const {
    return index >= 0 && index < static_cast<int>(items_.size()) &&
        items_[index].enabled && !items_[index].command_id.empty();
}

void ContextMenuComponent::MoveSelection(int direction) {
    const int count = static_cast<int>(items_.size());
    int next = selected_ < 0 ? (direction > 0 ? count - 1 : 0) : selected_;
    for (int step = 0; step < count; ++step) {
        next = (next + direction + count) % count;
        if (IsEnabled(next)) {
            selected_ = next;
            return;
        }
    }
}

void ContextMenuComponent::ExecuteSelected() {
    if (!IsEnabled(selected_)) return;

    const std::string command_id = items_[selected_].command_id;
    Close();
    if (execute_) execute_(command_id);
}

ftxui::Element ContextMenuComponent::OnRender() {
    return RenderIn(ftxui::Terminal::Size());
}

ftxui::Element ContextMenuComponent::RenderIn(ftxui::Dimensions viewport) {
    using namespace ftxui;
    if (!IsOpen()) return emptyElement();
    if (viewport.dimx < 3 || viewport.dimy < 3) {
        Close();
        return emptyElement();
    }

    int width = 4;
    for (const auto& item : items_) {
        width = std::max(width, string_width(item.label) + 4);
    }
    width = std::min(width, viewport.dimx);
    const int count = static_cast<int>(items_.size());
    const int visible = std::min(count, viewport.dimy - 2);
    const int height = visible + 2;
    if (selected_ >= 0) {
        first_row_ = std::clamp(first_row_, std::max(0, selected_ - visible + 1), selected_);
    }
    first_row_ = std::min(first_row_, count - visible);

    const int x = std::clamp(anchor_x_, 0, viewport.dimx - width);
    const int y = std::clamp(anchor_y_, 0, viewport.dimy - height);
    Elements rows;
    for (int index = first_row_; index < first_row_ + visible; ++index) {
        const auto& item = items_[index];
        if (item.command_id.empty()) {
            rows.push_back(separator() | color(theme_.modal_border));
            continue;
        }
        Element row = hbox({text(" " + item.label + " "), filler()});
        if (index == selected_) {
            row = row | bgcolor(theme_.modal_selected_item_bg) |
                color(theme_.modal_selected_item_fg);
        } else if (IsEnabled(index)) {
            row = row | color(theme_.modal_text_color);
        } else {
            row = row | color(theme_.button_disabled_fg) | dim;
        }
        rows.push_back(std::move(row));
    }

    Element popup = vbox(std::move(rows)) | borderStyled(LIGHT, theme_.modal_border) |
        bgcolor(theme_.modal_background) | clear_under | size(WIDTH, EQUAL, width) |
        size(HEIGHT, EQUAL, height) | reflect(box_);
    return vbox({
        emptyElement() | size(HEIGHT, EQUAL, y),
        hbox({emptyElement() | size(WIDTH, EQUAL, x), std::move(popup)}),
    });
}

bool ContextMenuComponent::OnEvent(ftxui::Event event) {
    using namespace ftxui;
    if (!IsOpen() || event == Event::Custom || event.is_cursor_position()) return false;

    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        const bool wheel = mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown;
        const bool left = mouse.button == Mouse::Left;
        if (!box_.Contain(mouse.x, mouse.y)) {
            if (mouse.motion == Mouse::Pressed || wheel) Close();
            if (left && mouse.motion == Mouse::Released) pressed_ = -1;
            return false;
        }
        if (wheel) {
            MoveSelection(mouse.button == Mouse::WheelUp ? -1 : 1);
            return true;
        }
        const int index = mouse.y > box_.y_min && mouse.y < box_.y_max &&
            mouse.x > box_.x_min && mouse.x < box_.x_max
            ? first_row_ + mouse.y - box_.y_min - 1 : -1;
        if (left && mouse.motion == Mouse::Released) {
            if (std::exchange(pressed_, -1) == index && IsEnabled(index)) {
                selected_ = index;
                ExecuteSelected();
            }
        } else if (left && mouse.motion == Mouse::Pressed) {
            pressed_ = IsEnabled(index) ? index : -1;
            if (pressed_ >= 0) selected_ = pressed_;
        } else if (mouse.motion == Mouse::Moved && IsEnabled(index)) {
            selected_ = index;
        }
        return true;
    }

    if (event == Event::ArrowDown || event == Event::ArrowUp) {
        MoveSelection(event == Event::ArrowDown ? 1 : -1);
    } else if (event == Event::Home || event == Event::End) {
        selected_ = -1;
        MoveSelection(event == Event::Home ? 1 : -1);
    } else if (event == Event::Return) {
        ExecuteSelected();
    } else if (event == Event::Escape) {
        Close();
    } else {
        Close();
        return false;
    }
    return true;
}

} // namespace textlt
