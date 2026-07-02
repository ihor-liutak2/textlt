#include "editor_component.hpp"

#include "editor_utils.hpp"

#include <algorithm>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

namespace textlt {
namespace {

constexpr int kScrollbarColumns = 2;

} // namespace

bool EditorComponent::HandleMouseEvent(ftxui::Event event) {
    auto mouse = event.mouse();
    if (!doc_) return false;

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

    const bool smart_word_wrap = config_ && config_->smart_word_wrap;
    const size_t visible_width = VisibleTextWidth();
    auto effective_total = [&]() -> size_t {
        if (!smart_word_wrap) return doc_->lines.size();
        return utils::WordWrapTotalVisualRows(doc_->lines, visible_width);
    };
    auto max_scroll_y = [&]() -> size_t {
        if (!smart_word_wrap) {
            return doc_->lines.size() > visible_height
                ? doc_->lines.size() - visible_height : 0;
        }
        return utils::WordWrapMaxScrollY(doc_->lines, visible_height, visible_width);
    };
    auto scrollbar_thumb_height = [&]() -> size_t {
        const size_t eff = effective_total();
        if (eff <= visible_height) {
            return visible_height;
        }
        return std::min(
            visible_height,
            std::max<size_t>(
                visible_height >= 2 ? 2 : 1,
                (visible_height * visible_height) / eff));
    };

    const bool needs_scrollbar = effective_total() > visible_height;
    const int scrollbar_x_min = editor_box_.x_max - kScrollbarColumns + 1;
    const bool on_scrollbar_column =
    needs_scrollbar &&
    inside_visible_viewport &&
    mouse.x >= scrollbar_x_min &&
    mouse.x <= editor_box_.x_max;
    auto clamp_cursor_to_visible_scroll = [&]() {
        if (doc_->cursor_row < scroll_y_) {
            doc_->cursor_row = scroll_y_;
            doc_->cursor_col = std::min(doc_->cursor_col, doc_->lines[doc_->cursor_row].size());
        } else if (doc_->cursor_row >= scroll_y_ + visible_height) {
            doc_->cursor_row = std::min(scroll_y_ + visible_height - 1, doc_->lines.size() - 1);
            doc_->cursor_col = std::min(doc_->cursor_col, doc_->lines[doc_->cursor_row].size());
        }
    };
    auto jump_to_scrollbar_y = [&](int screen_y) {
        const size_t eff = effective_total();
        if (eff <= visible_height) {
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

        const int relative_y = std::clamp(screen_y - viewport_y_min, 0, static_cast<int>(visible_height) - 1);
        const int thumb_center_offset = static_cast<int>(thumb_height / 2);
        const int target_thumb_top = std::clamp(
            relative_y - thumb_center_offset,
            0,
            static_cast<int>(available_track_space));
        const size_t target_visual_row =
        (static_cast<size_t>(target_thumb_top) * (eff - visible_height)) / available_track_space;
        scroll_y_ = utils::WordWrapLineAtVisualRow(doc_->lines, target_visual_row, visible_width);
        clamp_cursor_to_visible_scroll();
    };
    auto drag_scrollbar_to_y = [&](int screen_y) {
        const size_t eff = effective_total();
        if (eff <= visible_height) {
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

        const size_t start_visual_row = utils::WordWrapVisualRowAtLine(
            doc_->lines, drag_start_scroll_y_, visible_width);
        const int drag_delta_y = screen_y - drag_start_y_;
        const long long visual_delta =
        (static_cast<long long>(drag_delta_y) * static_cast<long long>(eff - visible_height)) /
        static_cast<long long>(available_track_space);
        const long long target_visual =
        static_cast<long long>(start_visual_row) + visual_delta;
        const size_t clamped_visual = static_cast<size_t>(
            std::clamp<long long>(target_visual, 0, static_cast<long long>(eff - visible_height)));
        scroll_y_ = utils::WordWrapLineAtVisualRow(doc_->lines, clamped_visual, visible_width);
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
        if (doc_->cursor_row >= scroll_y_ + visible_height) {
            doc_->cursor_row = scroll_y_ + visible_height - 1;
            doc_->cursor_col = std::min(doc_->cursor_col, doc_->lines[doc_->cursor_row].size());
        }
        return true;
    }

    if (inside_editor && mouse.button == ftxui::Mouse::WheelDown) {
        EndTypingGroup();
        // Rename the variable to avoid conflicting with the lambda name
        const size_t max_scroll_val = max_scroll_y();
        scroll_y_ = std::min(scroll_y_ + 3, max_scroll_val);

        if (doc_->cursor_row < scroll_y_) {
            doc_->cursor_row = scroll_y_;
            doc_->cursor_col = std::min(doc_->cursor_col, doc_->lines[doc_->cursor_row].size());
        }
        return true;
    }

    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
        EndTypingGroup();
        TakeFocus();
        if (on_scrollbar_column) {
            is_dragging_scrollbar_ = true;
            mouse_selecting_ = false;
            doc_->SetSelectionActive(false);
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
        const int relative_x = mouse.x - editor_box_.x_min - line_number_gutter_width;

        size_t clicked_row = scroll_y_;
        size_t segment_start = scroll_x_;
        size_t segment_end = doc_->lines[clicked_row].size();
        if (config_ && config_->smart_word_wrap) {
            size_t visual_row = 0;
            const size_t target_visual_row = static_cast<size_t>(std::max(0, relative_y));
            for (size_t row = scroll_y_; row < doc_->lines.size(); ++row) {
                const auto segments =
                    utils::BuildUtf8WrapSegments(doc_->lines[row], VisibleTextWidth());
                if (target_visual_row < visual_row + segments.size()) {
                    clicked_row = row;
                    segment_start = segments[target_visual_row - visual_row].start;
                    segment_end = segments[target_visual_row - visual_row].end;
                    break;
                }
                visual_row += segments.size();
                clicked_row = row;
                segment_start = segments.back().start;
                segment_end = segments.back().end;
            }
        } else {
            const size_t max_row = doc_->lines.size() - 1;
            const int raw_clicked_row = static_cast<int>(scroll_y_) + relative_y;
            clicked_row = std::clamp(
                static_cast<size_t>(std::max(0, raw_clicked_row)), size_t{0}, max_row);
        }

        const size_t clicked_col = std::min(segment_end, relative_x <= 0
            ? segment_start
            : utils::Utf8ByteIndexAtDisplayColumn(
                doc_->lines[clicked_row], segment_start, static_cast<size_t>(relative_x)));

        const bool extend_selection = mouse_selecting_ || mouse.shift;
        if (!extend_selection) {
            doc_->SetSelectionAnchor(clicked_row, clicked_col);
            doc_->SetSelectionActive(true);
            mouse_selecting_ = true;
        } else if (mouse.shift) {
            BeginSelection();
            mouse_selecting_ = true;
        } else {
            doc_->SetSelectionActive(true);
        }

        doc_->cursor_row = clicked_row;
        doc_->cursor_col = clicked_col;
        UpdateScroll();
        return true;
    }

    if (inside_editor && mouse.motion != ftxui::Mouse::Pressed && mouse.motion != ftxui::Mouse::Released) {
        return true;
    }

    return false;
}

} // namespace textlt
