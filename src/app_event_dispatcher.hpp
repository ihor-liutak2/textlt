#pragma once

#include "ftxui/component/event.hpp"

#include <cstddef>

namespace textlt {

class TextltApp;

class AppEventDispatcher {
public:
    explicit AppEventDispatcher(TextltApp& app);

    bool Handle(ftxui::Event event);
    bool HandleBodyEvent(ftxui::Event event);
    bool HandleEditorPaneEvent(size_t pane_index, ftxui::Event event);

private:
    bool RestoreClosedModalFocus();
    bool HandleActiveModalEvent(const ftxui::Event& event);
    bool HandleFindPanelEvent(const ftxui::Event& event);
    bool HandleMainEvent(const ftxui::Event& event);
    bool HandleSidebarEvent(const ftxui::Event& event);
    bool HandleEditorEvent(const ftxui::Event& event);
    bool HandleFunctionKeyEvent(const ftxui::Event& event);

    TextltApp& app_;
};

} // namespace textlt
