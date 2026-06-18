#pragma once

#include "ftxui/component/event.hpp"

namespace textlt {

class EditorComponent;

class EditorInputController {
public:
    bool HandleEvent(EditorComponent& editor, ftxui::Event event);
};

} // namespace textlt
