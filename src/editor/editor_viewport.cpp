#include "editor/editor_viewport.hpp"

#include <algorithm>

#include "editor/document_session.hpp"
#include "editor_config.hpp"
#include "editor_utils.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt {
namespace {

constexpr size_t kScrollbarColumns = 2;
constexpr size_t kFallbackViewportWidth = 80;
constexpr size_t kMaxSafeViewportWidth = 1000;
constexpr size_t kMaxSafeViewportHeight = 300;

} // namespace

void EditorViewport::Reset() {
    scroll_x = 0;
    scroll_y = 0;
    mouse_selecting = false;
    is_dragging_scrollbar = false;
    drag_start_scroll_y = 0;
    drag_start_y = 0;
}

void EditorViewport::SetBox(ftxui::Box value) {
    box = value;
}

const ftxui::Box& EditorViewport::Box() const {
    return box;
}

size_t EditorViewport::VisibleHeight() const {
    if (box.y_max < box.y_min) {
        return 1;
    }

    const long long total_height =
        static_cast<long long>(box.y_max) -
        static_cast<long long>(box.y_min) + 1;
    if (total_height <= 0) {
        return 1;
    }

    // The viewport box can be uninitialized during early component composition
    // or before a pane has been reflected by FTXUI. Clamp the value so a bad
    // box cannot allocate an enormous number of rendered rows.
    return std::min(static_cast<size_t>(total_height), kMaxSafeViewportHeight);
}

size_t EditorViewport::VisibleTextWidth(const DocumentSession* session, const EditorConfig* config) const {
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

    const bool show_line_numbers = config && config->show_line_numbers;
    const size_t line_number_columns =
        show_line_numbers ? ftxui::string_width(LineNumberText(0, LineNumberWidth(session))) : 0;

    if (line_number_columns + kScrollbarColumns >= total_width) {
        return 1;
    }
    return total_width - line_number_columns - kScrollbarColumns;
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

void EditorViewport::ScrollToCursor(DocumentSession& session, const EditorConfig* config) {
    session.ClampCursor();

    const size_t visible_height = VisibleHeight();
    const bool smart_word_wrap = config && config->smart_word_wrap;
    if (session.cursor_row >= scroll_y + visible_height) {
        scroll_y = session.cursor_row - visible_height + 1;
    }
    if (session.cursor_row < scroll_y) {
        scroll_y = session.cursor_row;
    }

    if (session.lines.size() <= visible_height && !smart_word_wrap) {
        scroll_y = 0;
    } else if (smart_word_wrap) {
        const size_t visible_width = VisibleTextWidth(&session, config);
        const size_t max_scroll_y = utils::WordWrapMaxScrollY(session.lines, visible_height, visible_width);
        if (session.cursor_row > max_scroll_y) {
            scroll_y = session.cursor_row;
        }
        scroll_y = std::min(scroll_y, max_scroll_y);
    } else {
        const size_t max_scroll_y = session.lines.size() - visible_height;
        scroll_y = std::min(scroll_y, max_scroll_y);
    }

    const size_t visible_width = VisibleTextWidth(&session, config);
    if (config && config->smart_word_wrap) {
        scroll_x = 0;
        return;
    }

    if (session.cursor_col < scroll_x) {
        scroll_x = session.cursor_col;
    }

    const std::string& current_line = session.lines[session.cursor_row];
    while (scroll_x < session.cursor_col &&
           utils::Utf8DisplayWidth(current_line, scroll_x, session.cursor_col) >= visible_width) {
        scroll_x = utils::NextUtf8CodepointStart(current_line, scroll_x);
    }

    if (utils::Utf8DisplayWidth(current_line) < visible_width) {
        scroll_x = 0;
    }
}

} // namespace textlt
