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

    (void)normal;
    (void)focused;
    (void)selected;
    (void)disabled;

    return 0;
}
