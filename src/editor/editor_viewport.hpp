#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

#include "editor/editor_cursor_state.hpp"

namespace textlt {

class DocumentSession;
struct EditorConfig;

struct EditorViewportOptions {
    bool distraction_mode = false;
    bool center_text = false;
    bool show_line_numbers = true;
    bool show_scrollbar = true;
    size_t max_text_width = 0;
    size_t column_gap = 0;
    size_t top_padding = 0;
    size_t bottom_padding = 0;
};

struct EditorViewportMouseCallbacks {
    std::function<void()> end_typing_group;
    std::function<void()> take_focus;
};

struct HorizontalScrollbar {
    size_t width = 0;
    size_t max_offset = 0;
    size_t thumb_width = 0;
    size_t thumb_start = 0;
};

class EditorViewport {
public:
    void Reset();

    EditorCursorState& CursorState();
    const EditorCursorState& CursorState() const;

    void SetBox(ftxui::Box box);
    const ftxui::Box& Box() const;

    void SetOptions(EditorViewportOptions options);
    const EditorViewportOptions& Options() const;
    bool IsDistractionMode() const;

    size_t BoxWidth() const;
    size_t RawHeight() const;
    size_t ContentTopPadding() const;
    size_t ContentBottomPadding() const;
    size_t VisibleHeight() const;
    bool ShouldShowLineNumbers(const EditorConfig* config) const;
    bool ShouldShowScrollbar() const;
    size_t ScrollbarColumns() const;
    size_t VisibleTextWidth(const DocumentSession* session, const EditorConfig* config) const;
    HorizontalScrollbar HorizontalScrollMetrics(const DocumentSession& session, const EditorConfig* config) const;
    bool HasManualHorizontalScroll() const;
    void ResetHorizontalScroll();
    size_t TextLeftPadding(const DocumentSession* session, const EditorConfig* config) const;
    size_t LineNumberWidth(const DocumentSession* session) const;
    std::string LineNumberText(size_t line_index, size_t width) const;
    size_t EstimatedWrappedTotalRows(const DocumentSession& session, const EditorConfig* config) const;
    size_t EstimatedWrappedVisualRowAtLine(
        const DocumentSession& session,
        const EditorConfig* config,
        size_t line_index) const;
    size_t EstimatedWrappedLineAtVisualRow(
        const DocumentSession& session,
        const EditorConfig* config,
        size_t visual_row) const;
    size_t EstimatedWrappedMaxScrollY(
        const DocumentSession& session,
        const EditorConfig* config,
        size_t visible_height) const;

    void ScrollToCursor(DocumentSession& session, const EditorConfig* config);
    bool HandleMouseEvent(
        DocumentSession& session,
        const EditorConfig* config,
        ftxui::Event event,
        const EditorViewportMouseCallbacks& callbacks = {});

    size_t scroll_x = 0; // Display cells, shared across unwrapped lines.
    size_t scroll_y = 0;
    size_t wrapped_scroll_offset = 0;
    bool mouse_selecting = false;
    bool is_dragging_scrollbar = false;
    size_t drag_start_scroll_y = 0;
    int drag_start_y = 0;
    std::chrono::steady_clock::time_point last_text_click_time_{};
    size_t last_text_click_row_ = 0;
    size_t last_text_click_col_ = 0;
    int text_click_count_ = 0;
    ftxui::Box box;
    ftxui::Box horizontal_scrollbar_box;
    EditorCursorState cursor_state;

private:
    struct HorizontalPan {
        std::uint64_t buffer_version;
        size_t row;
        size_t col;
    };

    bool HandleHorizontalScrollbar(DocumentSession& session, const EditorConfig* config,
                                   const ftxui::Mouse& mouse, const EditorViewportMouseCallbacks& callbacks);

    struct EstimatedWrapMetricsCache {
        bool valid = false;
        std::uint64_t buffer_version = 0;
        size_t visible_width = 0;
        size_t line_count = 0;
        size_t total_visual_rows = 1;
        std::vector<size_t> prefix_visual_rows;
    };

    const EstimatedWrapMetricsCache& EstimatedWrapMetricsFor(
        const DocumentSession& session,
        const EditorConfig* config) const;
    std::pair<size_t, size_t> PositionAtMouse(
        const DocumentSession& session,
        const EditorConfig* config,
        const ftxui::Mouse& mouse) const;

    EditorViewportOptions options_;
    bool horizontal_scrollbar_visible_ = false;
    std::optional<HorizontalPan> horizontal_pan_;
    std::optional<int> horizontal_grab_;
    mutable const DocumentSession* width_session_ = nullptr;
    mutable std::uint64_t width_version_ = 0;
    mutable size_t longest_line_width_ = 0;
    mutable EstimatedWrapMetricsCache estimated_wrap_metrics_cache_;
};

} // namespace textlt
