#pragma once

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

} // namespace textlt
