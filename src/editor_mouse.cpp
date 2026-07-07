#include "editor_component.hpp"

#include "editor_utils.hpp"

#include <algorithm>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt {
namespace {

constexpr int kScrollbarColumns = 2;

} // namespace

bool EditorComponent::HandleMouseEvent(ftxui::Event event) {
    auto mouse = event.mouse();
    if (!session_) return false;

    const bool inside_editor =
    mouse.x >= viewport_->box.x_min && mouse.x <= viewport_->box.x_max &&
    mouse.y >= viewport_->box.y_min && mouse.y <= viewport_->box.y_max;
    const size_t visible_height = VisibleHeight();
    const int viewport_y_min = viewport_->box.y_min;
    const int viewport_y_max =
    viewport_->box.y_min + static_cast<int>(visible_height) - 1;
    const bool inside_visible_viewport =
    mouse.x >= viewport_->box.x_min && mouse.x <= viewport_->box.x_max &&
    mouse.y >= viewport_y_min && mouse.y <= viewport_y_max;

    const bool smart_word_wrap = config_ && config_->smart_word_wrap;
    const size_t visible_width = VisibleTextWidth();
    auto effective_total = [&]() -> size_t {
        if (!smart_word_wrap) return session_->lines.size();
        return utils::WordWrapTotalVisualRows(session_->lines, visible_width);
    };
    auto max_scroll_y = [&]() -> size_t {
        if (!smart_word_wrap) {
            return session_->lines.size() > visible_height
                ? session_->lines.size() - visible_height : 0;
        }
        return utils::WordWrapMaxScrollY(session_->lines, visible_height, visible_width);
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
    const int scrollbar_x_min = viewport_->box.x_max - kScrollbarColumns + 1;
    const bool on_scrollbar_column =
    needs_scrollbar &&
    inside_visible_viewport &&
    mouse.x >= scrollbar_x_min &&
    mouse.x <= viewport_->box.x_max;
    auto clamp_cursor_to_visible_scroll = [&]() {
        if (session_->cursor_row < viewport_->scroll_y) {
            session_->cursor_row = viewport_->scroll_y;
            session_->cursor_col = std::min(session_->cursor_col, session_->lines[session_->cursor_row].size());
        } else if (session_->cursor_row >= viewport_->scroll_y + visible_height) {
            session_->cursor_row = std::min(viewport_->scroll_y + visible_height - 1, session_->lines.size() - 1);
            session_->cursor_col = std::min(session_->cursor_col, session_->lines[session_->cursor_row].size());
        }
    };
    auto jump_to_scrollbar_y = [&](int screen_y) {
        const size_t eff = effective_total();
        if (eff <= visible_height) {
            viewport_->scroll_y = 0;
            return;
        }

        const size_t thumb_height = scrollbar_thumb_height();
        const size_t available_track_space = visible_height > thumb_height
        ? visible_height - thumb_height
        : 0;
        if (available_track_space == 0) {
            viewport_->scroll_y = 0;
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
        viewport_->scroll_y = utils::WordWrapLineAtVisualRow(session_->lines, target_visual_row, visible_width);
        clamp_cursor_to_visible_scroll();
    };
    auto drag_scrollbar_to_y = [&](int screen_y) {
        const size_t eff = effective_total();
        if (eff <= visible_height) {
            viewport_->scroll_y = 0;
            return;
        }

        const size_t thumb_height = scrollbar_thumb_height();
        const size_t available_track_space = visible_height > thumb_height
        ? visible_height - thumb_height
        : 0;
        if (available_track_space == 0) {
            viewport_->scroll_y = 0;
            return;
        }

        const size_t start_visual_row = utils::WordWrapVisualRowAtLine(
            session_->lines, viewport_->drag_start_scroll_y, visible_width);
        const int drag_delta_y = screen_y - viewport_->drag_start_y;
        const long long visual_delta =
        (static_cast<long long>(drag_delta_y) * static_cast<long long>(eff - visible_height)) /
        static_cast<long long>(available_track_space);
        const long long target_visual =
        static_cast<long long>(start_visual_row) + visual_delta;
        const size_t clamped_visual = static_cast<size_t>(
            std::clamp<long long>(target_visual, 0, static_cast<long long>(eff - visible_height)));
        viewport_->scroll_y = utils::WordWrapLineAtVisualRow(session_->lines, clamped_visual, visible_width);
        clamp_cursor_to_visible_scroll();
    };
    auto position_at_mouse = [&](const ftxui::Mouse& hit_mouse) {
        const int relative_y = hit_mouse.y - viewport_->box.y_min;
        const bool show_line_numbers = config_ && config_->show_line_numbers;
        const int line_number_gutter_width = show_line_numbers
            ? static_cast<int>(ftxui::string_width(LineNumberText(0, LineNumberWidth())))
            : 0;
        const int relative_x = hit_mouse.x - viewport_->box.x_min - line_number_gutter_width;

        size_t clicked_row = viewport_->scroll_y;
        size_t segment_start = viewport_->scroll_x;
        size_t segment_end = session_->lines[clicked_row].size();
        if (config_ && config_->smart_word_wrap) {
            size_t visual_row = 0;
            const size_t target_visual_row = static_cast<size_t>(std::max(0, relative_y));
            for (size_t row = viewport_->scroll_y; row < session_->lines.size(); ++row) {
                const auto segments =
                    utils::BuildUtf8WrapSegments(session_->lines[row], VisibleTextWidth());
                if (segments.empty()) {
                    continue;
                }
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
            const size_t max_row = session_->lines.size() - 1;
            const int raw_clicked_row = static_cast<int>(viewport_->scroll_y) + relative_y;
            clicked_row = std::clamp(
                static_cast<size_t>(std::max(0, raw_clicked_row)), size_t{0}, max_row);
        }

        const size_t clicked_col = std::min(segment_end, relative_x <= 0
            ? segment_start
            : utils::Utf8ByteIndexAtDisplayColumn(
                session_->lines[clicked_row], segment_start, static_cast<size_t>(relative_x)));
        return std::pair<size_t, size_t>{clicked_row, clicked_col};
    };

    if (mouse.motion == ftxui::Mouse::Released) {
        viewport_->is_dragging_scrollbar = false;
        viewport_->mouse_selecting = false;
        return true;
    }

    if (viewport_->is_dragging_scrollbar && mouse.motion == ftxui::Mouse::Pressed) {
        drag_scrollbar_to_y(mouse.y);
        return true;
    }

    if (!inside_editor && !viewport_->mouse_selecting) {
        return false;
    }

    if (inside_editor && mouse.button == ftxui::Mouse::WheelUp) {
        EndTypingGroup();
        viewport_->scroll_y = viewport_->scroll_y > 3 ? viewport_->scroll_y - 3 : 0;
        if (session_->cursor_row >= viewport_->scroll_y + visible_height) {
            session_->cursor_row = viewport_->scroll_y + visible_height - 1;
            session_->cursor_col = std::min(session_->cursor_col, session_->lines[session_->cursor_row].size());
        }
        return true;
    }

    if (inside_editor && mouse.button == ftxui::Mouse::WheelDown) {
        EndTypingGroup();
        // Rename the variable to avoid conflicting with the lambda name
        const size_t max_scroll_val = max_scroll_y();
        viewport_->scroll_y = std::min(viewport_->scroll_y + 3, max_scroll_val);

        if (session_->cursor_row < viewport_->scroll_y) {
            session_->cursor_row = viewport_->scroll_y;
            session_->cursor_col = std::min(session_->cursor_col, session_->lines[session_->cursor_row].size());
        }
        return true;
    }

    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
        EndTypingGroup();
        TakeFocus();
        if (on_scrollbar_column) {
            viewport_->is_dragging_scrollbar = true;
            viewport_->mouse_selecting = false;
            session_->SetSelectionActive(false);
            jump_to_scrollbar_y(mouse.y);
            viewport_->drag_start_y = mouse.y;
            viewport_->drag_start_scroll_y = viewport_->scroll_y;
            return true;
        }

        const auto [clicked_row, clicked_col] = position_at_mouse(mouse);
        const bool extend_existing_selection = mouse.shift && session_->HasSelection();
        const size_t anchor_row = session_->selection.anchor_y;
        const size_t anchor_col = session_->selection.anchor_x;
        session_->SetCursorPosition(clicked_row, clicked_col);
        if (extend_existing_selection) {
            session_->selection.anchor_y = anchor_row;
            session_->selection.anchor_x = anchor_col;
            session_->SetSelectionActive(true);
        } else {
            session_->SetSelectionAnchor(clicked_row, clicked_col);
            session_->SetSelectionActive(false);
        }
        viewport_->mouse_selecting = true;
        UpdateScroll();
        return true;
    }

    if (viewport_->mouse_selecting &&
        inside_editor &&
        mouse.motion != ftxui::Mouse::Pressed &&
        mouse.motion != ftxui::Mouse::Released) {
        const auto [clicked_row, clicked_col] = position_at_mouse(mouse);
        if (session_->cursor_row != clicked_row || session_->cursor_col != clicked_col) {
            session_->SetSelectionActive(true);
        }
        session_->cursor_row = clicked_row;
        session_->cursor_col = clicked_col;
        UpdateScroll();
        return true;
    }

    if (inside_editor && mouse.motion != ftxui::Mouse::Pressed && mouse.motion != ftxui::Mouse::Released) {
        return true;
    }

    return false;
}

} // namespace textlt
