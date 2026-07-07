#pragma once

#include <cstddef>
#include <string>

#include "ftxui/dom/elements.hpp"

namespace textlt {

class DocumentSession;
class EditorConfig;

class EditorViewport {
public:
    void Reset();

    void SetBox(ftxui::Box box);
    const ftxui::Box& Box() const;

    size_t VisibleHeight() const;
    size_t VisibleTextWidth(const DocumentSession* session, const EditorConfig* config) const;
    size_t LineNumberWidth(const DocumentSession* session) const;
    std::string LineNumberText(size_t line_index, size_t width) const;

    void ScrollToCursor(DocumentSession& session, const EditorConfig* config);

    size_t scroll_x = 0;
    size_t scroll_y = 0;
    bool mouse_selecting = false;
    bool is_dragging_scrollbar = false;
    size_t drag_start_scroll_y = 0;
    int drag_start_y = 0;
    ftxui::Box box;
};

} // namespace textlt
