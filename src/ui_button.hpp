#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "theme.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
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

inline std::string ButtonCaptionText(const ButtonSpec& spec) {
    if (spec.icon.empty()) {
        return spec.caption;
    }
    if (spec.caption.empty()) {
        return spec.icon;
    }
    return spec.icon + " " + spec.caption;
}

inline std::string PadButtonCaption(std::string caption, ButtonSize size) {
    switch (size) {
        case ButtonSize::Compact:
            return caption;
        case ButtonSize::Normal:
            return " " + caption + " ";
        case ButtonSize::Wide:
            return "  " + caption + "  ";
    }
    return " " + caption + " ";
}

inline bool ButtonStateUsesBackground(ButtonState state) {
    return state == ButtonState::Focused || state == ButtonState::Selected;
}

inline ftxui::Element ApplyButtonStateBackground(ftxui::Element element,
                                                 const ButtonStyle& style,
                                                 ButtonState state,
                                                 bool force_background = false) {
    if (force_background || ButtonStateUsesBackground(state)) {
        element |= ftxui::bgcolor(style.background);
    }
    return element;
}

inline ftxui::Element RenderButton(const Theme& theme,
                                   const ButtonSpec& spec,
                                   bool focused = false) {
    const ButtonState state = ResolveButtonState(spec, focused);
    const ButtonStyle style = ResolveButtonStyle(theme, spec.role, state);
    const std::string caption = PadButtonCaption(ButtonCaptionText(spec), spec.size);

    switch (spec.variant) {
        case ButtonVariant::AccentBar: {
            ftxui::Element element = ftxui::hbox({
                ftxui::text("▌") | ftxui::color(style.accent),
                ftxui::text(caption) | ftxui::color(style.text),
            });
            return ApplyButtonStateBackground(std::move(element), style, state);
        }
        case ButtonVariant::Pill: {
            ftxui::Element element = ftxui::text(caption) | ftxui::color(style.text);
            return ApplyButtonStateBackground(std::move(element), style, state, true);
        }
        case ButtonVariant::ColoredBrackets: {
            ftxui::Element element = ftxui::hbox({
                ftxui::text("[") | ftxui::color(style.accent),
                ftxui::text(caption) | ftxui::color(style.text),
                ftxui::text("]") | ftxui::color(style.accent),
            });
            return ApplyButtonStateBackground(std::move(element), style, state);
        }
        case ButtonVariant::Minimal: {
            ftxui::Element element = ftxui::text(ButtonCaptionText(spec)) |
                ftxui::color(state == ButtonState::Normal ? style.accent : style.text);
            return ApplyButtonStateBackground(std::move(element), style, state);
        }
        case ButtonVariant::Shadow: {
            ftxui::Element body = ftxui::text(caption) | ftxui::color(style.text);
            body = ApplyButtonStateBackground(std::move(body), style, state, true);
            std::string shadow_text;
            for (int index = 0; index < (std::max)(1, static_cast<int>(caption.size())); ++index) {
                shadow_text += "▄";
            }
            return ftxui::vbox({
                std::move(body),
                ftxui::text(shadow_text) | ftxui::color(style.shadow),
            });
        }
        case ButtonVariant::Bracket:
        default: {
            ftxui::Element element = ftxui::text("[" + caption + "]") |
                ftxui::color(state == ButtonState::Normal ? style.accent : style.text);
            return ApplyButtonStateBackground(std::move(element), style, state);
        }
    }
}

inline ftxui::ButtonOption MakeButtonOption(const Theme* theme,
                                            ButtonSpec spec,
                                            std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [theme, spec = std::move(spec)](const ftxui::EntryState& state) {
        const Theme fallback_theme;
        const Theme& resolved_theme = theme ? *theme : fallback_theme;
        return RenderButton(resolved_theme, spec, state.focused || state.active);
    };
    return option;
}

inline ftxui::Component MakeButton(const Theme* theme,
                                   ButtonSpec spec,
                                   std::function<void()> on_click) {
    return ftxui::Button(MakeButtonOption(theme, std::move(spec), std::move(on_click)));
}

} // namespace textlt
