#pragma once

#include <algorithm>
#include <cctype>
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
    AccentEdges,
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
        case ButtonVariant::AccentEdges:
            return "AccentEdges";
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


inline std::string ButtonLabelKey(std::string label) {
    label.erase(std::remove(label.begin(), label.end(), '['), label.end());
    label.erase(std::remove(label.begin(), label.end(), ']'), label.end());
    while (!label.empty() && std::isspace(static_cast<unsigned char>(label.front()))) {
        label.erase(label.begin());
    }
    while (!label.empty() && std::isspace(static_cast<unsigned char>(label.back()))) {
        label.pop_back();
    }
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return label;
}

inline bool ButtonLabelContains(const std::string& label_key, const std::string& needle) {
    return label_key.find(needle) != std::string::npos;
}

inline ButtonRole ButtonRoleFromLabel(const std::string& label) {
    const std::string key = ButtonLabelKey(label);
    if (key.empty()) {
        return ButtonRole::Default;
    }
    if (key == "close" || key == "cancel" || key == "back" || key == "no" ||
        key == "esc") {
        return ButtonRole::Cancel;
    }
    if (ButtonLabelContains(key, "delete") || ButtonLabelContains(key, "remove") ||
        ButtonLabelContains(key, "discard") || ButtonLabelContains(key, "don't save") ||
        ButtonLabelContains(key, "force") || ButtonLabelContains(key, "drop") ||
        ButtonLabelContains(key, "erase") || ButtonLabelContains(key, "trash")) {
        return ButtonRole::Danger;
    }
    if (ButtonLabelContains(key, "reset") || ButtonLabelContains(key, "clear") ||
        ButtonLabelContains(key, "overwrite") || ButtonLabelContains(key, "replace") ||
        ButtonLabelContains(key, "cut") || ButtonLabelContains(key, "stop") ||
        ButtonLabelContains(key, "disconnect") || ButtonLabelContains(key, "rebase") ||
        ButtonLabelContains(key, "unstage") || ButtonLabelContains(key, "abort")) {
        return ButtonRole::Warning;
    }
    if (ButtonLabelContains(key, "prev") || ButtonLabelContains(key, "next conn") ||
        ButtonLabelContains(key, "home") || ButtonLabelContains(key, "current dir") ||
        ButtonLabelContains(key, "parent") || key == "up" || key == ".." ||
        ButtonLabelContains(key, "local") || ButtonLabelContains(key, "remote")) {
        return ButtonRole::Navigation;
    }
    if (ButtonLabelContains(key, "play") || ButtonLabelContains(key, "pause") ||
        ButtonLabelContains(key, "resume") || key == "next") {
        return ButtonRole::Media;
    }
    if (ButtonLabelContains(key, "refresh") || ButtonLabelContains(key, "reload") ||
        ButtonLabelContains(key, "copy") || ButtonLabelContains(key, "test") ||
        ButtonLabelContains(key, "browse") || ButtonLabelContains(key, "folder") ||
        ButtonLabelContains(key, "registry") || ButtonLabelContains(key, "help") ||
        ButtonLabelContains(key, "preview") || ButtonLabelContains(key, "path") ||
        ButtonLabelContains(key, "token") || ButtonLabelContains(key, "settings") ||
        ButtonLabelContains(key, "log")) {
        return ButtonRole::Utility;
    }
    if (key == "ok" || key == "yes" || key == "done" ||
        ButtonLabelContains(key, "apply") || ButtonLabelContains(key, "save") ||
        ButtonLabelContains(key, "select") || ButtonLabelContains(key, "confirm") ||
        ButtonLabelContains(key, "open") || ButtonLabelContains(key, "add") ||
        ButtonLabelContains(key, "create") || ButtonLabelContains(key, "install") ||
        ButtonLabelContains(key, "download") || ButtonLabelContains(key, "authorize") ||
        ButtonLabelContains(key, "submit") || ButtonLabelContains(key, "set ") ||
        ButtonLabelContains(key, "assign") || ButtonLabelContains(key, "connect") ||
        ButtonLabelContains(key, "commit") || ButtonLabelContains(key, "checkout") ||
        ButtonLabelContains(key, "push") || ButtonLabelContains(key, "fetch") ||
        ButtonLabelContains(key, "pull") || ButtonLabelContains(key, "run") ||
        ButtonLabelContains(key, "generate") || ButtonLabelContains(key, "export") ||
        ButtonLabelContains(key, "import")) {
        return ButtonRole::Primary;
    }
    return ButtonRole::Secondary;
}

inline ButtonVariant ButtonVariantForRole(ButtonRole role) {
    switch (role) {
        case ButtonRole::Default:
        case ButtonRole::Primary:
        case ButtonRole::Secondary:
        case ButtonRole::Success:
        case ButtonRole::Warning:
        case ButtonRole::Danger:
        case ButtonRole::Cancel:
        case ButtonRole::Utility:
        case ButtonRole::Navigation:
        case ButtonRole::Tab:
        case ButtonRole::Toggle:
        case ButtonRole::Media:
            return ButtonVariant::AccentEdges;
    }
    return ButtonVariant::AccentEdges;
}

inline ButtonSpec ButtonSpecFromLabel(std::string label,
                                      ButtonRole role = ButtonRole::Default,
                                      ButtonVariant variant = ButtonVariant::Bracket,
                                      ButtonSize size = ButtonSize::Normal,
                                      std::string icon = {}) {
    if (role == ButtonRole::Default) {
        role = ButtonRoleFromLabel(label);
    }
    if (variant == ButtonVariant::Bracket) {
        variant = ButtonVariantForRole(role);
    }
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.role = role;
    spec.variant = variant;
    spec.size = size;
    spec.icon = std::move(icon);
    return spec;
}

inline ButtonSpec MakeButtonSpec(std::string caption,
                                 ButtonRole role = ButtonRole::Default,
                                 ButtonVariant variant = ButtonVariant::Bracket,
                                 ButtonSize size = ButtonSize::Normal,
                                 std::string icon = {},
                                 bool selected = false,
                                 bool enabled = true) {
    ButtonSpec spec = ButtonSpecFromLabel(std::move(caption),
                                          role,
                                          variant,
                                          size,
                                          std::move(icon));
    spec.selected = selected;
    spec.enabled = enabled;
    return spec;
}

inline ButtonSpec RoleButtonSpec(ButtonRole role,
                                 std::string caption,
                                 std::string icon = {},
                                 ButtonSize size = ButtonSize::Normal,
                                 bool selected = false,
                                 bool enabled = true) {
    return MakeButtonSpec(std::move(caption),
                          role,
                          ButtonVariantForRole(role),
                          size,
                          std::move(icon),
                          selected,
                          enabled);
}

inline ButtonSpec PrimaryButtonSpec(std::string caption,
                                    std::string icon = {},
                                    ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Primary, std::move(caption), std::move(icon), size);
}

inline ButtonSpec SecondaryButtonSpec(std::string caption,
                                      std::string icon = {},
                                      ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Secondary, std::move(caption), std::move(icon), size);
}

inline ButtonSpec SuccessButtonSpec(std::string caption,
                                    std::string icon = {},
                                    ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Success, std::move(caption), std::move(icon), size);
}

inline ButtonSpec WarningButtonSpec(std::string caption,
                                    std::string icon = {},
                                    ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Warning, std::move(caption), std::move(icon), size);
}

inline ButtonSpec DangerButtonSpec(std::string caption,
                                   std::string icon = {},
                                   ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Danger, std::move(caption), std::move(icon), size);
}

inline ButtonSpec CancelButtonSpec(std::string caption = "Close",
                                   std::string icon = {},
                                   ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Cancel, std::move(caption), std::move(icon), size);
}

inline ButtonSpec UtilityButtonSpec(std::string caption,
                                    std::string icon = {},
                                    ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Utility, std::move(caption), std::move(icon), size);
}

inline ButtonSpec NavigationButtonSpec(std::string caption,
                                       std::string icon = {},
                                       ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Navigation, std::move(caption), std::move(icon), size);
}

inline ButtonSpec TabButtonSpec(std::string caption,
                                bool selected = false,
                                std::string icon = {},
                                ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Tab, std::move(caption), std::move(icon), size, selected);
}

inline ButtonSpec ToggleButtonSpec(std::string caption,
                                   bool selected = false,
                                   std::string icon = {},
                                   ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Toggle, std::move(caption), std::move(icon), size, selected);
}

inline ButtonSpec MediaButtonSpec(std::string caption,
                                  std::string icon = {},
                                  ButtonSize size = ButtonSize::Normal) {
    return RoleButtonSpec(ButtonRole::Media, std::move(caption), std::move(icon), size);
}

inline ButtonSpec WithButtonVariant(ButtonSpec spec, ButtonVariant variant) {
    spec.variant = variant;
    return spec;
}

inline ButtonSpec WithButtonSize(ButtonSpec spec, ButtonSize size) {
    spec.size = size;
    return spec;
}

inline ButtonSpec WithButtonIcon(ButtonSpec spec, std::string icon) {
    spec.icon = std::move(icon);
    return spec;
}

inline ButtonSpec WithButtonSelected(ButtonSpec spec, bool selected = true) {
    spec.selected = selected;
    return spec;
}

inline ButtonSpec WithButtonEnabled(ButtonSpec spec, bool enabled = true) {
    spec.enabled = enabled;
    return spec;
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
        case ButtonVariant::AccentEdges: {
            ftxui::Element element = ftxui::hbox({
                ftxui::text("▌") | ftxui::color(style.accent),
                ftxui::text(caption) | ftxui::color(style.text),
                ftxui::text("▐") | ftxui::color(style.accent),
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


inline ftxui::Element RenderRoleButton(const Theme& theme,
                                      ButtonRole role,
                                     std::string caption,
                                     bool focused = false,
                                     std::string icon = {},
                                     ButtonSize size = ButtonSize::Normal,
                                     bool selected = false) {
    return RenderButton(theme,
                        RoleButtonSpec(role,
                                       std::move(caption),
                                       std::move(icon),
                                       size,
                                       selected),
                        focused);
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

inline ftxui::Component MakeRoleButton(const Theme* theme,
                                       ButtonRole role,
                                       std::string caption,
                                       std::function<void()> on_click,
                                       std::string icon = {},
                                       ButtonSize size = ButtonSize::Normal) {
    return MakeButton(theme,
                      RoleButtonSpec(role, std::move(caption), std::move(icon), size),
                      std::move(on_click));
}

inline ftxui::Component MakePrimaryButton(const Theme* theme,
                                          std::string caption,
                                          std::function<void()> on_click,
                                          std::string icon = {},
                                          ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Primary,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeSecondaryButton(const Theme* theme,
                                            std::string caption,
                                            std::function<void()> on_click,
                                            std::string icon = {},
                                            ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Secondary,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeSuccessButton(const Theme* theme,
                                          std::string caption,
                                          std::function<void()> on_click,
                                          std::string icon = {},
                                          ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Success,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeWarningButton(const Theme* theme,
                                          std::string caption,
                                          std::function<void()> on_click,
                                          std::string icon = {},
                                          ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Warning,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeDangerButton(const Theme* theme,
                                         std::string caption,
                                         std::function<void()> on_click,
                                         std::string icon = {},
                                         ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Danger,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeCancelButton(const Theme* theme,
                                         std::function<void()> on_click,
                                         std::string caption = "Close",
                                         std::string icon = {},
                                         ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Cancel,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeUtilityButton(const Theme* theme,
                                          std::string caption,
                                          std::function<void()> on_click,
                                          std::string icon = {},
                                          ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Utility,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeNavigationButton(const Theme* theme,
                                             std::string caption,
                                             std::function<void()> on_click,
                                             std::string icon = {},
                                             ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Navigation,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

inline ftxui::Component MakeMediaButton(const Theme* theme,
                                        std::string caption,
                                        std::function<void()> on_click,
                                        std::string icon = {},
                                        ButtonSize size = ButtonSize::Normal) {
    return MakeRoleButton(theme,
                          ButtonRole::Media,
                          std::move(caption),
                          std::move(on_click),
                          std::move(icon),
                          size);
}

} // namespace textlt
