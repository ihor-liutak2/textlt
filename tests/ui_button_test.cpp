#include "ui_button.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace textlt;

    assert(std::string(ToString(ButtonRole::Primary)) == "Primary");
    assert(std::string(ToString(ButtonVariant::AccentBar)) == "AccentBar");
    assert(std::string(ToString(ButtonState::Disabled)) == "Disabled");
    assert(std::string(ToString(ButtonSize::Compact)) == "Compact");

    ButtonSpec save_button;
    save_button.caption = "Save";
    save_button.role = ButtonRole::Primary;
    save_button.variant = ButtonVariant::AccentBar;

    assert(ButtonCaptionText(save_button) == "Save");
    assert(PadButtonCaption("Save", ButtonSize::Compact) == "Save");
    assert(PadButtonCaption("Save", ButtonSize::Normal) == " Save ");
    assert(PadButtonCaption("Save", ButtonSize::Wide) == "  Save  ");

    assert(ResolveButtonState(save_button, false) == ButtonState::Normal);
    assert(ResolveButtonState(save_button, true) == ButtonState::Focused);

    save_button.selected = true;
    assert(ResolveButtonState(save_button, false) == ButtonState::Selected);
    assert(ResolveButtonState(save_button, true) == ButtonState::Selected);

    save_button.enabled = false;
    assert(ResolveButtonState(save_button, false) == ButtonState::Disabled);
    assert(ResolveButtonState(save_button, true) == ButtonState::Disabled);

    Theme theme;
    const ButtonStyle normal = ResolveButtonStyle(theme, ButtonRole::Primary, ButtonState::Normal);
    const ButtonStyle focused = ResolveButtonStyle(theme, ButtonRole::Primary, ButtonState::Focused);
    const ButtonStyle selected = ResolveButtonStyle(theme, ButtonRole::Primary, ButtonState::Selected);
    const ButtonStyle disabled = ResolveButtonStyle(theme, ButtonRole::Primary, ButtonState::Disabled);

    ButtonSpec icon_button;
    icon_button.caption = "Play";
    icon_button.icon = "▶";
    icon_button.role = ButtonRole::Media;
    icon_button.variant = ButtonVariant::AccentBar;
    assert(ButtonCaptionText(icon_button) == "▶ Play");

    ButtonSpec disabled_button = save_button;
    disabled_button.enabled = false;
    assert(ResolveButtonState(disabled_button, true) == ButtonState::Disabled);

    RenderButton(theme, save_button, false);
    RenderButton(theme, save_button, true);
    RenderButton(theme, icon_button, false);

    ButtonSpec colored = save_button;
    colored.variant = ButtonVariant::ColoredBrackets;
    RenderButton(theme, colored, false);

    ButtonSpec pill = save_button;
    pill.variant = ButtonVariant::Pill;
    RenderButton(theme, pill, false);

    ButtonSpec minimal = save_button;
    minimal.variant = ButtonVariant::Minimal;
    RenderButton(theme, minimal, false);

    ButtonSpec shadow = save_button;
    shadow.variant = ButtonVariant::Shadow;
    RenderButton(theme, shadow, false);

    auto button = MakeButton(&theme, icon_button, [] {});
    assert(button != nullptr);

    (void)normal;
    (void)focused;
    (void)selected;
    (void)disabled;

    return 0;
}
