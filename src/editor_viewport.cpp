#include "editor_component.hpp"

#include <algorithm>

namespace textlt {

size_t EditorComponent::VisibleHeight() const {
    if (editor_box_.y_max < editor_box_.y_min) {
        return 1;
    }

    const size_t total_height =
        static_cast<size_t>(editor_box_.y_max - editor_box_.y_min + 1);
    if (bottom_overlay_rows_ >= total_height) {
        return 1;
    }

    return total_height - bottom_overlay_rows_;
}

size_t EditorComponent::VisibleTextWidth() const {
    static constexpr size_t kScrollbarColumns = 1;
    const size_t total_width = editor_box_.x_max >= editor_box_.x_min
        ? static_cast<size_t>(editor_box_.x_max - editor_box_.x_min + 1)
        : 80;
    const bool show_line_numbers = config_ && config_->show_line_numbers;
    const size_t line_number_columns =
        show_line_numbers ? LineNumberText(0, LineNumberWidth()).size() : 0;
    if (line_number_columns + kScrollbarColumns >= total_width) {
        return 1;
    }
    return total_width - line_number_columns - kScrollbarColumns;
}

void EditorComponent::UpdateScroll() {
    ClampCursorToBuffer();
    const size_t visible_height = VisibleHeight();
    if (cursor_y_ >= scroll_y_ + visible_height) {
        scroll_y_ = cursor_y_ - visible_height + 1;
    }
    if (cursor_y_ < scroll_y_) {
        scroll_y_ = cursor_y_;
    }
    if (text_lines_.size() <= visible_height) {
        scroll_y_ = 0;
    } else {
        const size_t max_scroll_y = text_lines_.size() - visible_height;
        scroll_y_ = std::min(scroll_y_, max_scroll_y);
    }

    const size_t visible_width = VisibleTextWidth();
    if (cursor_x_ >= scroll_x_ + visible_width) {
        scroll_x_ = cursor_x_ - visible_width + 1;
    }
    if (cursor_x_ < scroll_x_) {
        scroll_x_ = cursor_x_;
    }

    const size_t current_line_size = text_lines_[cursor_y_].size();
    if (current_line_size < visible_width) {
        scroll_x_ = 0;
    } else {
        const size_t max_scroll_x = current_line_size - visible_width + 1;
        scroll_x_ = std::min(scroll_x_, max_scroll_x);
    }
}

} // namespace textlt
