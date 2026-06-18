#include "editor_component.hpp"

namespace textlt {

bool EditorComponent::OnEvent(ftxui::Event event) {
    if (event.is_mouse() && HandleMouseEvent(event)) {
        return true;
    }

    if (input_controller_.HandleEvent(*this, event)) {
        return true;
    }

    return ComponentBase::OnEvent(event);
}

} // namespace textlt
