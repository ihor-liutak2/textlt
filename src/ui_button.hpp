#pragma once

#include <string>

#include "theme.hpp"
#include "ftxui/screen/color.hpp"

namespace textlt {

enum class ButtonRole {
    Default,
    Primary,
    Secondary,
    Success,
    Warning,
    Danger,
    Cancel,
    Utility,
    Navigation,
    Tab,
    Toggle,
    Media,
};

enum class ButtonVariant {
    Bracket,
    AccentBar,
    Pill,
    ColoredBrackets,
    Minimal,
    Shadow,
};

enum class ButtonState {
    Normal,
    Focused,
    Selected,
    Disabled,
};

enum class ButtonSize {
    Compact,
    Normal,
    Wide,
};

struct ButtonSpec {
    std::string caption;
    ButtonRole role = ButtonRole::Default;
    ButtonVariant variant = ButtonVariant::Bracket;
    ButtonSize size = ButtonSize::Normal;
    bool enabled = true;
    bool selected = false;
    std::string icon;
};

struct ButtonStyle {
    ftxui::Color accent = ftxui::Color::Cyan;
    ftxui::Color text = ftxui::Color::White;
    ftxui::Color background = ftxui::Color::Black;
    ftxui::Color bracket = ftxui::Color::Cyan;
    ftxui::Color shadow = ftxui::Color::GrayDark;
};

inline const char* ToString(ButtonRole role) {
    switch (role) {
        case ButtonRole::Default:
            return "Default";
        case ButtonRole::Primary:
            return "Primary";
        case ButtonRole::Secondary:
            return "Secondary";
        case ButtonRole::Success:
            return "Success";
        case ButtonRole::Warning:
            return "Warning";
        case ButtonRole::Danger:
            return "Danger";
        case ButtonRole::Cancel:
            return "Cancel";
        case ButtonRole::Utility:
            return "Utility";
        case ButtonRole::Navigation:
            return "Navigation";
        case ButtonRole::Tab:
            return "Tab";
        case ButtonRole::Toggle:
            return "Toggle";
        case ButtonRole::Media:
            return "Media";
    }
    return "Default";
}

inline const char* ToString(ButtonVariant variant) {
    switch (variant) {
        case ButtonVariant::Bracket:
            return "Bracket";
        case ButtonVariant::AccentBar:
            return "AccentBar";
        case ButtonVariant::Pill:
            return "Pill";
        case ButtonVariant::ColoredBrackets:
            return "ColoredBrackets";
        case ButtonVariant::Minimal:
            return "Minimal";
        case ButtonVariant::Shadow:
            return "Shadow";
    }
    return "Bracket";
}

inline const char* ToString(ButtonState state) {
    switch (state) {
        case ButtonState::Normal:
            return "Normal";
        case ButtonState::Focused:
            return "Focused";
        case ButtonState::Selected:
            return "Selected";
        case ButtonState::Disabled:
            return "Disabled";
    }
    return "Normal";
}

inline const char* ToString(ButtonSize size) {
    switch (size) {
        case ButtonSize::Compact:
            return "Compact";
        case ButtonSize::Normal:
            return "Normal";
        case ButtonSize::Wide:
            return "Wide";
    }
    return "Normal";
}

inline ButtonState ResolveButtonState(const ButtonSpec& spec, bool focused) {
    if (!spec.enabled) {
        return ButtonState::Disabled;
    }
    if (spec.selected) {
        return ButtonState::Selected;
    }
    if (focused) {
        return ButtonState::Focused;
    }
    return ButtonState::Normal;
}

inline ftxui::Color ResolveButtonRoleAccent(const Theme& theme, ButtonRole role) {
    switch (role) {
        case ButtonRole::Default:
            return theme.button_default;
        case ButtonRole::Primary:
            return theme.button_primary;
        case ButtonRole::Secondary:
            return theme.button_secondary;
        case ButtonRole::Success:
            return theme.button_success;
        case ButtonRole::Warning:
            return theme.button_warning;
        case ButtonRole::Danger:
            return theme.button_danger;
        case ButtonRole::Cancel:
            return theme.button_cancel;
        case ButtonRole::Utility:
            return theme.button_utility;
        case ButtonRole::Navigation:
            return theme.button_navigation;
        case ButtonRole::Tab:
            return theme.button_tab;
        case ButtonRole::Toggle:
            return theme.button_toggle;
        case ButtonRole::Media:
            return theme.button_media;
    }
    return theme.button_default;
}

inline ButtonStyle ResolveButtonStyle(const Theme& theme,
                                      ButtonRole role,
                                      ButtonState state) {
    ButtonStyle style;
    style.accent = ResolveButtonRoleAccent(theme, role);
    style.text = theme.button_text;
    style.background = theme.modal_background;
    style.bracket = theme.button_bracket;
    style.shadow = theme.button_shadow;

    switch (state) {
        case ButtonState::Normal:
            break;
        case ButtonState::Focused:
            style.text = theme.button_focused_fg;
            style.background = theme.button_focused_bg;
            break;
        case ButtonState::Selected:
            style.text = theme.button_selected_fg;
            style.background = theme.button_selected_bg;
            break;
        case ButtonState::Disabled:
            style.accent = theme.button_disabled_fg;
            style.text = theme.button_disabled_fg;
            style.bracket = theme.button_disabled_fg;
            break;
    }
    return style;
}

inline ButtonStyle ResolveButtonStyle(const Theme& theme,
                                      const ButtonSpec& spec,
                                      bool focused = false) {
    return ResolveButtonStyle(theme, spec.role, ResolveButtonState(spec, focused));
}

} // namespace textlt
