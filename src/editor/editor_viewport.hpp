#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

class DocumentSession;
class EditorConfig;

struct EditorViewportOptions {
    bool distraction_mode = false;
    bool center_text = false;
    bool show_line_numbers = true;
    bool show_scrollbar = true;
    size_t max_text_width = 0;
    size_t top_padding = 0;
    size_t bottom_padding = 0;
};

struct EditorViewportMouseCallbacks {
    std::function<void()> end_typing_group;
    std::function<void()> take_focus;
};

class EditorViewport {
public:
    void Reset();

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
    size_t TextLeftPadding(const DocumentSession* session, const EditorConfig* config) const;
    size_t LineNumberWidth(const DocumentSession* session) const;
    std::string LineNumberText(size_t line_index, size_t width) const;

    void ScrollToCursor(DocumentSession& session, const EditorConfig* config);
    bool HandleMouseEvent(
        DocumentSession& session,
        const EditorConfig* config,
        ftxui::Event event,
        const EditorViewportMouseCallbacks& callbacks = {});

    size_t scroll_x = 0;
    size_t scroll_y = 0;
    bool mouse_selecting = false;
    bool is_dragging_scrollbar = false;
    size_t drag_start_scroll_y = 0;
    int drag_start_y = 0;
    ftxui::Box box;

private:
    std::pair<size_t, size_t> PositionAtMouse(
        const DocumentSession& session,
        const EditorConfig* config,
        const ftxui::Mouse& mouse) const;

    EditorViewportOptions options_;
};

} // namespace textlt
