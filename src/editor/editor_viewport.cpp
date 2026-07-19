#include "editor/editor_viewport.hpp"

#include <algorithm>
#include <iterator>
#include <string>

#include "editor/document_session.hpp"
#include "editor_config.hpp"
#include "editor_utils.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt {
namespace {

constexpr size_t kDefaultScrollbarColumns = 2;
constexpr size_t kFallbackViewportWidth = 80;
constexpr size_t kMaxSafeViewportWidth = 1000;
constexpr size_t kMaxSafeViewportHeight = 300;
constexpr size_t kMouseWheelScrollLines = 3;

size_t EstimateWrappedLineRows(const std::string& line, size_t visible_width) {
    visible_width = std::max<size_t>(1, visible_width);
    const size_t display_width = utils::Utf8DisplayWidth(line);
    return std::max<size_t>(1, (display_width + visible_width - 1) / visible_width);
}

} // namespace

void EditorViewport::Reset() {
    scroll_x = 0;
    scroll_y = 0;
    mouse_selecting = false;
    is_dragging_scrollbar = false;
    drag_start_scroll_y = 0;
    drag_start_y = 0;
    last_text_click_time_ = {};
    last_text_click_row_ = 0;
    last_text_click_col_ = 0;
    text_click_count_ = 0;
    cursor_state = {};
}

EditorCursorState& EditorViewport::CursorState() {
    return cursor_state;
}

const EditorCursorState& EditorViewport::CursorState() const {
    return cursor_state;
}

void EditorViewport::SetBox(ftxui::Box value) {
    box = value;
}

const ftxui::Box& EditorViewport::Box() const {
    return box;
}

void EditorViewport::SetOptions(EditorViewportOptions options) {
    options_ = options;
}

const EditorViewportOptions& EditorViewport::Options() const {
    return options_;
}

bool EditorViewport::IsDistractionMode() const {
    return options_.distraction_mode;
}

size_t EditorViewport::BoxWidth() const {
    size_t total_width = kFallbackViewportWidth;
    if (box.x_max >= box.x_min) {
        const long long measured_width =
            static_cast<long long>(box.x_max) -
            static_cast<long long>(box.x_min) + 1;
        if (measured_width > 0 &&
            static_cast<size_t>(measured_width) <= kMaxSafeViewportWidth) {
            total_width = static_cast<size_t>(measured_width);
        }
    }
    return total_width;
}

size_t EditorViewport::RawHeight() const {
    if (box.y_max < box.y_min) {
        return 1;
    }

    const long long total_height =
        static_cast<long long>(box.y_max) -
        static_cast<long long>(box.y_min) + 1;
    if (total_height <= 0) {
        return 1;
    }

    return std::min(static_cast<size_t>(total_height), kMaxSafeViewportHeight);
}

size_t EditorViewport::ContentTopPadding() const {
    const size_t raw_height = RawHeight();
    return std::min(options_.top_padding, raw_height > 1 ? raw_height - 1 : 0);
}

size_t EditorViewport::ContentBottomPadding() const {
    const size_t raw_height = RawHeight();
    const size_t top_padding = ContentTopPadding();
    const size_t remaining = raw_height > top_padding + 1 ? raw_height - top_padding - 1 : 0;
    return std::min(options_.bottom_padding, remaining);
}

size_t EditorViewport::VisibleHeight() const {
    const size_t raw_height = RawHeight();
    const size_t padding = ContentTopPadding() + ContentBottomPadding();
    if (padding >= raw_height) {
        return 1;
    }
    return raw_height - padding;
}

bool EditorViewport::ShouldShowLineNumbers(const EditorConfig* config) const {
    return options_.show_line_numbers && config && config->show_line_numbers;
}

bool EditorViewport::ShouldShowScrollbar() const {
    return options_.show_scrollbar;
}

size_t EditorViewport::ScrollbarColumns() const {
    return ShouldShowScrollbar() ? kDefaultScrollbarColumns : 0;
}

size_t EditorViewport::VisibleTextWidth(const DocumentSession* session, const EditorConfig* config) const {
    const size_t total_width = BoxWidth();
    const size_t line_number_columns = ShouldShowLineNumbers(config)
        ? ftxui::string_width(LineNumberText(0, LineNumberWidth(session)))
        : 0;
    const size_t scrollbar_columns = ScrollbarColumns();

    if (line_number_columns + scrollbar_columns >= total_width) {
        return 1;
    }

    size_t available_text_width = total_width - line_number_columns - scrollbar_columns;
    if (options_.max_text_width > 0) {
        available_text_width = std::min(available_text_width, options_.max_text_width);
    }
    return std::max<size_t>(available_text_width, 1);
}

size_t EditorViewport::TextLeftPadding(const DocumentSession* session, const EditorConfig* config) const {
    if (!options_.center_text && options_.max_text_width == 0) {
        return 0;
    }

    const size_t total_width = BoxWidth();
    const size_t line_number_columns = ShouldShowLineNumbers(config)
        ? ftxui::string_width(LineNumberText(0, LineNumberWidth(session)))
        : 0;
    const size_t scrollbar_columns = ScrollbarColumns();
    if (line_number_columns + scrollbar_columns >= total_width) {
        return 0;
    }

    const size_t available_text_width = total_width - line_number_columns - scrollbar_columns;
    const size_t effective_text_width = VisibleTextWidth(session, config);
    if (available_text_width <= effective_text_width) {
        return 0;
    }
    return options_.center_text ? (available_text_width - effective_text_width) / 2 : 0;
}

size_t EditorViewport::LineNumberWidth(const DocumentSession* session) const {
    return session ? std::to_string(session->lines.size()).size() : 1;
}

std::string EditorViewport::LineNumberText(size_t line_index, size_t width) const {
    std::string line_number = std::to_string(line_index + 1);
    if (line_number.size() < width) {
        line_number.insert(line_number.begin(), width - line_number.size(), ' ');
    }
    return line_number + " │ ";
}

const EditorViewport::EstimatedWrapMetricsCache& EditorViewport::EstimatedWrapMetricsFor(
    const DocumentSession& session,
    const EditorConfig* config) const {
    const size_t visible_width = std::max<size_t>(1, VisibleTextWidth(&session, config));
    const std::uint64_t buffer_version = session.buffer.Version();
    const size_t line_count = session.lines.size();

    if (estimated_wrap_metrics_cache_.valid &&
        estimated_wrap_metrics_cache_.buffer_version == buffer_version &&
        estimated_wrap_metrics_cache_.visible_width == visible_width &&
        estimated_wrap_metrics_cache_.line_count == line_count) {
        return estimated_wrap_metrics_cache_;
    }

    auto& cache = estimated_wrap_metrics_cache_;
    cache.valid = true;
    cache.buffer_version = buffer_version;
    cache.visible_width = visible_width;
    cache.line_count = line_count;
    cache.prefix_visual_rows.assign(line_count + 1, 0);

    size_t total_rows = 0;
    for (size_t index = 0; index < line_count; ++index) {
        cache.prefix_visual_rows[index] = total_rows;
        total_rows += EstimateWrappedLineRows(session.lines[index], visible_width);
    }
    cache.prefix_visual_rows[line_count] = total_rows;
    cache.total_visual_rows = std::max<size_t>(1, total_rows);
    return cache;
}

size_t EditorViewport::EstimatedWrappedTotalRows(
    const DocumentSession& session,
    const EditorConfig* config) const {
    return EstimatedWrapMetricsFor(session, config).total_visual_rows;
}

size_t EditorViewport::EstimatedWrappedVisualRowAtLine(
    const DocumentSession& session,
    const EditorConfig* config,
    size_t line_index) const {
    const auto& cache = EstimatedWrapMetricsFor(session, config);
    if (cache.prefix_visual_rows.empty()) {
        return 0;
    }
    line_index = std::min(line_index, cache.line_count);
    return cache.prefix_visual_rows[line_index];
}

size_t EditorViewport::EstimatedWrappedLineAtVisualRow(
    const DocumentSession& session,
    const EditorConfig* config,
    size_t visual_row) const {
    const auto& cache = EstimatedWrapMetricsFor(session, config);
    if (cache.line_count == 0 || cache.prefix_visual_rows.size() < 2) {
        return 0;
    }

    visual_row = std::min(visual_row, cache.total_visual_rows - 1);
    const auto iterator = std::upper_bound(
        cache.prefix_visual_rows.begin(),
        cache.prefix_visual_rows.end(),
        visual_row);
    if (iterator == cache.prefix_visual_rows.begin()) {
        return 0;
    }

    const size_t line_index = static_cast<size_t>(
        std::distance(cache.prefix_visual_rows.begin(), iterator) - 1);
    return std::min(line_index, cache.line_count - 1);
}

size_t EditorViewport::EstimatedWrappedMaxScrollY(
    const DocumentSession& session,
    const EditorConfig* config,
    size_t visible_height) const {
    const size_t total_rows = EstimatedWrappedTotalRows(session, config);
    if (total_rows <= visible_height) {
        return 0;
    }
    return EstimatedWrappedLineAtVisualRow(session, config, total_rows - visible_height);
}

void EditorViewport::ScrollToCursor(DocumentSession& session, const EditorConfig* config) {
    session.ClampCursor();

    const size_t visible_height = VisibleHeight();
    const bool smart_word_wrap = options_.distraction_mode ||
        (config && config->smart_word_wrap);

    if (smart_word_wrap) {
        const size_t max_scroll_y = EstimatedWrappedMaxScrollY(session, config, visible_height);
        const size_t cursor_visual_row = EstimatedWrappedVisualRowAtLine(
            session, config, session.CursorRow());
        const size_t scroll_visual_row = EstimatedWrappedVisualRowAtLine(
            session, config, scroll_y);

        if (cursor_visual_row < scroll_visual_row) {
            scroll_y = session.CursorRow();
        } else if (cursor_visual_row >= scroll_visual_row + visible_height) {
            scroll_y = EstimatedWrappedLineAtVisualRow(
                session,
                config,
                cursor_visual_row - visible_height + 1);
        }

        scroll_y = std::min(scroll_y, max_scroll_y);
        scroll_x = 0;
        return;
    }

    if (session.CursorRow() >= scroll_y + visible_height) {
        scroll_y = session.CursorRow() - visible_height + 1;
    }
    if (session.CursorRow() < scroll_y) {
        scroll_y = session.CursorRow();
    }

    if (session.lines.size() <= visible_height) {
        scroll_y = 0;
    } else {
        const size_t max_scroll_y = session.lines.size() - visible_height;
        scroll_y = std::min(scroll_y, max_scroll_y);
    }

    const size_t visible_width = VisibleTextWidth(&session, config);

    if (session.CursorCol() < scroll_x) {
        scroll_x = session.CursorCol();
    }

    const std::string& current_line = session.lines[session.CursorRow()];
    while (scroll_x < session.CursorCol() &&
           utils::Utf8DisplayWidth(current_line, scroll_x, session.CursorCol()) >= visible_width) {
        scroll_x = utils::NextUtf8CodepointStart(current_line, scroll_x);
    }

    if (utils::Utf8DisplayWidth(current_line) < visible_width) {
        scroll_x = 0;
    }
}

std::pair<size_t, size_t> EditorViewport::PositionAtMouse(
    const DocumentSession& session,
    const EditorConfig* config,
    const ftxui::Mouse& mouse) const {
    const int relative_y =
        mouse.y - box.y_min - static_cast<int>(ContentTopPadding());
    const int line_number_gutter_width = ShouldShowLineNumbers(config)
        ? static_cast<int>(ftxui::string_width(LineNumberText(0, LineNumberWidth(&session))))
        : 0;
    const int text_left_padding = static_cast<int>(TextLeftPadding(&session, config));
    const int relative_x = mouse.x - box.x_min - line_number_gutter_width - text_left_padding;
    const bool smart_word_wrap = options_.distraction_mode ||
        (config && config->smart_word_wrap);

    size_t clicked_row = scroll_y;
    size_t segment_start = scroll_x;
    size_t segment_end = session.lines[clicked_row].size();
    if (smart_word_wrap) {
        size_t visual_row = 0;
        const size_t target_visual_row = static_cast<size_t>(std::max(0, relative_y));
        for (size_t row = scroll_y; row < session.lines.size(); ++row) {
            const size_t segments_needed = target_visual_row >= visual_row
                ? target_visual_row - visual_row + 1
                : 1;
            const auto segments = utils::BuildUtf8WrapSegmentsLimited(
                session.lines[row],
                VisibleTextWidth(&session, config),
                segments_needed);
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
        const size_t max_row = session.lines.size() - 1;
        const int raw_clicked_row = static_cast<int>(scroll_y) + relative_y;
        clicked_row = std::clamp(
            static_cast<size_t>(std::max(0, raw_clicked_row)), size_t{0}, max_row);
    }

    const size_t clicked_col = std::min(segment_end, relative_x <= 0
        ? segment_start
        : utils::Utf8ByteIndexAtDisplayColumn(
            session.lines[clicked_row], segment_start, static_cast<size_t>(relative_x)));
    return {clicked_row, clicked_col};
}

bool EditorViewport::HandleMouseEvent(
    DocumentSession& session,
    const EditorConfig* config,
    ftxui::Event event,
    const EditorViewportMouseCallbacks& callbacks) {
    session.SetActiveCursorState(&cursor_state);
    auto mouse = event.mouse();

    const bool inside_editor =
        mouse.x >= box.x_min && mouse.x <= box.x_max &&
        mouse.y >= box.y_min && mouse.y <= box.y_max;
    const size_t visible_height = VisibleHeight();
    const int viewport_y_min = box.y_min + static_cast<int>(ContentTopPadding());
    const int viewport_y_max =
        viewport_y_min + static_cast<int>(visible_height) - 1;
    const bool inside_visible_viewport =
        mouse.x >= box.x_min && mouse.x <= box.x_max &&
        mouse.y >= viewport_y_min && mouse.y <= viewport_y_max;

    const bool smart_word_wrap = options_.distraction_mode ||
        (config && config->smart_word_wrap);
    auto effective_total = [&]() -> size_t {
        return smart_word_wrap
            ? EstimatedWrappedTotalRows(session, config)
            : session.lines.size();
    };
    auto max_scroll_y = [&]() -> size_t {
        if (smart_word_wrap) {
            return EstimatedWrappedMaxScrollY(session, config, visible_height);
        }
        return session.lines.size() > visible_height
            ? session.lines.size() - visible_height
            : 0;
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

    const bool needs_scrollbar = ShouldShowScrollbar() &&
        effective_total() > visible_height;
    const int scrollbar_x_min = box.x_max - static_cast<int>(ScrollbarColumns()) + 1;
    const bool on_scrollbar_column =
        needs_scrollbar &&
        inside_visible_viewport &&
        mouse.x >= scrollbar_x_min &&
        mouse.x <= box.x_max;
    auto clamp_cursor_to_visible_scroll = [&]() {
        if (session.lines.empty()) {
            return;
        }

        if (smart_word_wrap) {
            session.CursorRow() = std::min(scroll_y, session.lines.size() - 1);
            session.CursorCol() = std::min(session.CursorCol(), session.lines[session.CursorRow()].size());
            return;
        }

        if (session.CursorRow() < scroll_y) {
            session.CursorRow() = scroll_y;
            session.CursorCol() = std::min(session.CursorCol(), session.lines[session.CursorRow()].size());
        } else if (session.CursorRow() >= scroll_y + visible_height) {
            session.CursorRow() = std::min(scroll_y + visible_height - 1, session.lines.size() - 1);
            session.CursorCol() = std::min(session.CursorCol(), session.lines[session.CursorRow()].size());
        }
    };
    auto jump_to_scrollbar_y = [&](int screen_y) {
        const size_t eff = effective_total();
        if (eff <= visible_height) {
            scroll_y = 0;
            return;
        }

        const size_t thumb_height = scrollbar_thumb_height();
        const size_t available_track_space = visible_height > thumb_height
            ? visible_height - thumb_height
            : 0;
        if (available_track_space == 0) {
            scroll_y = 0;
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
        scroll_y = smart_word_wrap
            ? EstimatedWrappedLineAtVisualRow(session, config, target_visual_row)
            : target_visual_row;
        clamp_cursor_to_visible_scroll();
    };
    auto drag_scrollbar_to_y = [&](int screen_y) {
        const size_t eff = effective_total();
        if (eff <= visible_height) {
            scroll_y = 0;
            return;
        }

        const size_t thumb_height = scrollbar_thumb_height();
        const size_t available_track_space = visible_height > thumb_height
            ? visible_height - thumb_height
            : 0;
        if (available_track_space == 0) {
            scroll_y = 0;
            return;
        }

        const size_t start_visual_row = smart_word_wrap
            ? EstimatedWrappedVisualRowAtLine(session, config, drag_start_scroll_y)
            : drag_start_scroll_y;
        const int drag_delta_y = screen_y - drag_start_y;
        const long long visual_delta =
            (static_cast<long long>(drag_delta_y) * static_cast<long long>(eff - visible_height)) /
            static_cast<long long>(available_track_space);
        const long long target_visual =
            static_cast<long long>(start_visual_row) + visual_delta;
        const size_t clamped_visual = static_cast<size_t>(
            std::clamp<long long>(target_visual, 0, static_cast<long long>(eff - visible_height)));
        scroll_y = smart_word_wrap
            ? EstimatedWrappedLineAtVisualRow(session, config, clamped_visual)
            : clamped_visual;
        clamp_cursor_to_visible_scroll();
    };

    if (mouse.motion == ftxui::Mouse::Released) {
        is_dragging_scrollbar = false;
        mouse_selecting = false;
        return true;
    }

    if (is_dragging_scrollbar && mouse.motion == ftxui::Mouse::Pressed) {
        drag_scrollbar_to_y(mouse.y);
        return true;
    }

    if (!inside_editor && !mouse_selecting) {
        return false;
    }

    if (inside_editor && mouse.button == ftxui::Mouse::WheelUp) {
        if (callbacks.end_typing_group) callbacks.end_typing_group();
        scroll_y = scroll_y > kMouseWheelScrollLines ? scroll_y - kMouseWheelScrollLines : 0;
        clamp_cursor_to_visible_scroll();
        return true;
    }

    if (inside_editor && mouse.button == ftxui::Mouse::WheelDown) {
        if (callbacks.end_typing_group) callbacks.end_typing_group();
        const size_t max_scroll_val = max_scroll_y();
        scroll_y = std::min(scroll_y + kMouseWheelScrollLines, max_scroll_val);
        clamp_cursor_to_visible_scroll();
        return true;
    }

    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
        if (callbacks.end_typing_group) callbacks.end_typing_group();
        if (callbacks.take_focus) callbacks.take_focus();
        if (on_scrollbar_column) {
            is_dragging_scrollbar = true;
            mouse_selecting = false;
            session.SetSelectionActive(false);
            jump_to_scrollbar_y(mouse.y);
            drag_start_y = mouse.y;
            drag_start_scroll_y = scroll_y;
            return true;
        }

        const auto [clicked_row, clicked_col] = PositionAtMouse(session, config, mouse);
        const size_t old_cursor_row = session.CursorRow();
        const size_t old_cursor_col = session.CursorCol();
        const bool extend_selection = mouse.shift;
        const size_t anchor_row = session.HasSelection()
            ? session.SelectionState().anchor_y
            : old_cursor_row;
        const size_t anchor_col = session.HasSelection()
            ? session.SelectionState().anchor_x
            : old_cursor_col;

        if (extend_selection) {
            text_click_count_ = 0;
            last_text_click_time_ = {};
            session.SetCursorPosition(clicked_row, clicked_col);
            session.SetSelectionAnchor(anchor_row, anchor_col);
            session.SetSelectionActive(
                clicked_row != anchor_row || clicked_col != anchor_col);
        } else {
            const auto now = std::chrono::steady_clock::now();
            const bool repeated_click = last_text_click_time_ !=
                    std::chrono::steady_clock::time_point{} &&
                now - last_text_click_time_ < std::chrono::milliseconds(350) &&
                clicked_row == last_text_click_row_ && clicked_col == last_text_click_col_;
            text_click_count_ = repeated_click ? std::min(3, text_click_count_ + 1) : 1;
            last_text_click_time_ = now;
            last_text_click_row_ = clicked_row;
            last_text_click_col_ = clicked_col;

            session.SetCursorPosition(clicked_row, clicked_col);
            if (text_click_count_ == 2 && !session.lines[clicked_row].empty()) {
                const std::string& line = session.lines[clicked_row];
                size_t position = std::min(clicked_col, line.size());
                if (position == line.size() && position > 0) {
                    position = utils::PreviousUtf8CodepointStart(line, position);
                }
                const bool word = position < line.size() &&
                    utils::IsWordCharacter(line[position]);
                size_t start = position;
                size_t end = position < line.size()
                    ? utils::NextUtf8CodepointStart(line, position)
                    : position;
                if (word) {
                    while (start > 0) {
                        const size_t previous = utils::PreviousUtf8CodepointStart(line, start);
                        if (!utils::IsWordCharacter(line[previous])) {
                            break;
                        }
                        start = previous;
                    }
                    while (end < line.size() && utils::IsWordCharacter(line[end])) {
                        end = utils::NextUtf8CodepointStart(line, end);
                    }
                }
                session.SetSelectionAnchor(clicked_row, start);
                session.SetCursorPosition(clicked_row, end);
                session.SetSelectionActive(end != start);
            } else if (text_click_count_ >= 3) {
                session.SetSelectionAnchor(clicked_row, 0);
                session.SetCursorPosition(clicked_row, session.lines[clicked_row].size());
                session.SetSelectionActive(!session.lines[clicked_row].empty());
            } else {
                session.SetSelectionAnchor(clicked_row, clicked_col);
                session.SetSelectionActive(false);
            }
        }
        mouse_selecting = true;
        ScrollToCursor(session, config);
        return true;
    }

    if (mouse_selecting &&
        inside_editor &&
        mouse.motion != ftxui::Mouse::Pressed &&
        mouse.motion != ftxui::Mouse::Released) {
        const auto [clicked_row, clicked_col] = PositionAtMouse(session, config, mouse);
        if (session.CursorRow() != clicked_row || session.CursorCol() != clicked_col) {
            session.SetSelectionActive(true);
        }
        session.CursorRow() = clicked_row;
        session.CursorCol() = clicked_col;
        ScrollToCursor(session, config);
        return true;
    }

    if (inside_editor && mouse.motion != ftxui::Mouse::Pressed && mouse.motion != ftxui::Mouse::Released) {
        return true;
    }

    return false;
}

} // namespace textlt
