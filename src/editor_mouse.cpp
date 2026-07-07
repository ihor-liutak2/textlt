#include "editor_component.hpp"

#include "editor/editor_viewport.hpp"

#include <utility>

namespace textlt {

bool EditorComponent::HandleMouseEvent(ftxui::Event event) {
    if (!session_ || !viewport_) {
        return false;
    }
    BindViewportCursorState();

    EditorViewportMouseCallbacks callbacks;
    callbacks.end_typing_group = [this]() { EndTypingGroup(); };
    callbacks.take_focus = [this]() { TakeFocus(); };
    return viewport_->HandleMouseEvent(*session_, config_, std::move(event), callbacks);
}

} // namespace textlt
