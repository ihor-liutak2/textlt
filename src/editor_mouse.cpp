#include "editor_component.hpp"

#include <algorithm>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

namespace textlt {

bool EditorComponent::HandleMouseEvent(ftxui::Event event) {
    auto mouse = event.mouse();

    const bool inside_editor =
        mouse.x >= editor_box_.x_min && mouse.x <= editor_box_.x_max &&
        mouse.y >= editor_box_.y_min && mouse.y <= editor_box_.y_max;

    if (mouse.motion == ftxui::Mouse::Released) {
        mouse_selecting_ = false;
        return true;
    }

    if (!inside_editor && !mouse_selecting_) {
        return false;
    }

    const size_t visible_height = VisibleHeight();
    if (inside_editor && mouse.button == ftxui::Mouse::WheelUp) {
        EndTypingGroup();
        scroll_y_ = scroll_y_ > 3 ? scroll_y_ - 3 : 0;
        if (cursor_y_ >= scroll_y_ + visible_height) {
            cursor_y_ = scroll_y_ + visible_height - 1;
            cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
        }
        return true;
    }

    if (inside_editor && mouse.button == ftxui::Mouse::WheelDown) {
        EndTypingGroup();
        const size_t max_scroll_y = text_lines_.size() > visible_height
            ? text_lines_.size() - visible_height
            : 0;
        scroll_y_ = std::min(scroll_y_ + 3, max_scroll_y);
        if (cursor_y_ < scroll_y_) {
            cursor_y_ = scroll_y_;
            cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
        }
        return true;
    }

    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
        EndTypingGroup();
        TakeFocus();

        const int relative_y = mouse.y - editor_box_.y_min;
        const bool show_line_numbers = config_ && config_->show_line_numbers;
        const int line_number_gutter_width = show_line_numbers
            ? static_cast<int>(LineNumberText(0, LineNumberWidth()).size())
            : 0;
        const int relative_x =
            mouse.x - editor_box_.x_min - line_number_gutter_width;

        const int max_row = static_cast<int>(text_lines_.size() - 1);
        const int raw_clicked_row = static_cast<int>(scroll_y_) + relative_y;
        const size_t clicked_row =
            static_cast<size_t>(std::clamp(raw_clicked_row, 0, max_row));

        const int raw_clicked_col = static_cast<int>(scroll_x_) + relative_x;
        const int max_col = static_cast<int>(text_lines_[clicked_row].size());
        const size_t clicked_col =
            static_cast<size_t>(std::clamp(raw_clicked_col, 0, max_col));

        const bool extend_selection = mouse_selecting_ || mouse.shift;
        if (!extend_selection) {
            selection_anchor_y_ = clicked_row;
            selection_anchor_x_ = clicked_col;
            has_selection_ = true;
            mouse_selecting_ = true;
        } else if (mouse.shift) {
            BeginSelection();
            mouse_selecting_ = true;
        } else {
            has_selection_ = true;
        }

        cursor_y_ = clicked_row;
        cursor_x_ = clicked_col;
        UpdateScroll();
        return true;
    }

    // Swallow hover/motion events inside the editor so background components do not react.
    if (inside_editor &&
        mouse.motion != ftxui::Mouse::Pressed &&
        mouse.motion != ftxui::Mouse::Released) {
        return true;
    }

    return false;
}

} // namespace textlt
