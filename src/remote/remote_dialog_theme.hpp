#pragma once

#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "theme.hpp"

namespace textlt {

inline bool IsLightRemoteTheme(const Theme& theme) {
    return theme.name.find("Light") != std::string::npos;
}

inline ftxui::Color RemoteDialogInputBackground(const Theme& theme, bool focused) {
    if (!focused) {
        return theme.modal_input_bg;
    }
    if (IsLightRemoteTheme(theme)) {
        return ftxui::Color::RGB(225, 225, 225);
    }
    return ftxui::Color::RGB(52, 52, 52);
}

inline ftxui::Element RemoteDialogInputTransform(const Theme& theme,
                                                 ftxui::InputState state) {
    const ftxui::Color background = RemoteDialogInputBackground(theme, state.focused);
    state.element |= ftxui::bgcolor(background);
    state.element |= ftxui::color(state.focused && state.is_placeholder
        ? background
        : theme.modal_input_fg);
    return state.element;
}

inline ftxui::Element RenderRemoteDialogInputFrame(
    const Theme& theme,
    const std::string& label,
    const ftxui::Component& component) {
    using namespace ftxui;
    const bool focused = component && component->Focused();
    Element input = component ? component->Render() : text("");
    input = input |
        bgcolor(RemoteDialogInputBackground(theme, focused)) |
        color(theme.modal_input_fg) |
        size(WIDTH, GREATER_THAN, 18) |
        borderStyled(LIGHT, focused ? theme.modal_accent : theme.modal_border);
    return vbox({
        text(" " + label) | color(theme.modal_accent),
        input,
    });
}

} // namespace textlt
