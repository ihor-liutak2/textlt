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
    const size_t visible_height = VisibleHeight();
    const int viewport_y_min = editor_box_.y_min;
    const int viewport_y_max =
        editor_box_.y_min + static_cast<int>(visible_height) - 1;
    const bool inside_visible_viewport =
        mouse.x >= editor_box_.x_min && mouse.x <= editor_box_.x_max &&
        mouse.y >= viewport_y_min && mouse.y <= viewport_y_max;
    const bool needs_scrollbar = text_lines_.size() > visible_height;
    const bool on_scrollbar_column =
        needs_scrollbar && inside_visible_viewport && mouse.x == editor_box_.x_max;

    auto max_scroll_y = [&]() {
        return text_lines_.size() > visible_height
            ? text_lines_.size() - visible_height
            : 0;
    };
    auto scrollbar_thumb_height = [&]() {
        if (text_lines_.size() <= visible_height) {
            return visible_height;
        }
        return std::min(
            visible_height,
            std::max<size_t>(
                visible_height >= 2 ? 2 : 1,
                (visible_height * visible_height) / text_lines_.size()));
    };
    auto clamp_cursor_to_visible_scroll = [&]() {
        if (cursor_y_ < scroll_y_) {
            cursor_y_ = scroll_y_;
            cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
        } else if (cursor_y_ >= scroll_y_ + visible_height) {
            cursor_y_ = std::min(scroll_y_ + visible_height - 1, text_lines_.size() - 1);
            cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
        }
    };
    auto jump_to_scrollbar_y = [&](int screen_y) {
        const size_t max_scroll = max_scroll_y();
        if (max_scroll == 0) {
            scroll_y_ = 0;
            return;
        }

        const size_t thumb_height = scrollbar_thumb_height();
        const size_t available_track_space = visible_height > thumb_height
            ? visible_height - thumb_height
            : 0;
        if (available_track_space == 0) {
            scroll_y_ = 0;
            return;
        }

        // Center the thumb around the clicked row, then map the track position
        // proportionally back into document line coordinates.
        const int relative_y = std::clamp(screen_y - viewport_y_min, 0, static_cast<int>(visible_height) - 1);
        const int thumb_center_offset = static_cast<int>(thumb_height / 2);
        const int target_thumb_top = std::clamp(
            relative_y - thumb_center_offset,
            0,
            static_cast<int>(available_track_space));
        scroll_y_ =
            (static_cast<size_t>(target_thumb_top) * max_scroll) / available_track_space;
        clamp_cursor_to_visible_scroll();
    };
    auto drag_scrollbar_to_y = [&](int screen_y) {
        const size_t max_scroll = max_scroll_y();
        if (max_scroll == 0) {
            scroll_y_ = 0;
            return;
        }

        const size_t thumb_height = scrollbar_thumb_height();
        const size_t available_track_space = visible_height > thumb_height
            ? visible_height - thumb_height
            : 0;
        if (available_track_space == 0) {
            scroll_y_ = 0;
            return;
        }

        // Dragging translates terminal-row delta into document-row delta using
        // the same track-to-document ratio as the renderer.
        const int drag_delta_y = screen_y - drag_start_y_;
        const long long scroll_delta =
            (static_cast<long long>(drag_delta_y) * static_cast<long long>(max_scroll)) /
            static_cast<long long>(available_track_space);
        const long long target_scroll =
            static_cast<long long>(drag_start_scroll_y_) + scroll_delta;
        scroll_y_ = static_cast<size_t>(
            std::clamp<long long>(target_scroll, 0, static_cast<long long>(max_scroll)));
        clamp_cursor_to_visible_scroll();
    };

    if (mouse.motion == ftxui::Mouse::Released) {
        is_dragging_scrollbar_ = false;
        mouse_selecting_ = false;
        return true;
    }

    if (is_dragging_scrollbar_ && mouse.motion == ftxui::Mouse::Pressed) {
        drag_scrollbar_to_y(mouse.y);
        return true;
    }

    if (!inside_editor && !mouse_selecting_) {
        return false;
    }

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
        if (on_scrollbar_column) {
            is_dragging_scrollbar_ = true;
            mouse_selecting_ = false;
            jump_to_scrollbar_y(mouse.y);
            drag_start_y_ = mouse.y;
            drag_start_scroll_y_ = scroll_y_;
            return true;
        }

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
