#include "ui_button.hpp"

#include <cassert>
#include <string>

int main() {
    using namespace textlt;

    assert(std::string(ToString(ButtonRole::Primary)) == "Primary");
    assert(std::string(ToString(ButtonVariant::AccentBar)) == "AccentBar");
    assert(std::string(ToString(ButtonState::Disabled)) == "Disabled");
    assert(std::string(ToString(ButtonSize::Compact)) == "Compact");


    assert(ButtonLabelKey("[Save]") == "save");
    assert(ButtonRoleFromLabel("Save") == ButtonRole::Primary);
    assert(ButtonRoleFromLabel("Apply layout") == ButtonRole::Primary);
    assert(ButtonRoleFromLabel("Delete") == ButtonRole::Danger);
    assert(ButtonRoleFromLabel("Confirm delete") == ButtonRole::Danger);
    assert(ButtonRoleFromLabel("Reset All") == ButtonRole::Warning);
    assert(ButtonRoleFromLabel("Refresh") == ButtonRole::Utility);
    assert(ButtonRoleFromLabel("Close") == ButtonRole::Cancel);
    assert(ButtonRoleFromLabel("OK") == ButtonRole::Primary);
    assert(ButtonRoleFromLabel("Drop cache") == ButtonRole::Danger);
    assert(ButtonRoleFromLabel("Disconnect") == ButtonRole::Warning);
    assert(ButtonRoleFromLabel("Copy path") == ButtonRole::Utility);
    assert(ButtonRoleFromLabel("Parent") == ButtonRole::Navigation);
    assert(ButtonRoleFromLabel("Generate") == ButtonRole::Primary);

    ButtonSpec inferred_button = ButtonSpecFromLabel("Delete");
    assert(inferred_button.role == ButtonRole::Danger);
    assert(inferred_button.variant == ButtonVariant::AccentBar);

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

    ButtonSpec primary_preset = PrimaryButtonSpec("Generate", "▶");
    assert(primary_preset.role == ButtonRole::Primary);
    assert(primary_preset.variant == ButtonVariant::AccentBar);
    assert(ButtonCaptionText(primary_preset) == "▶ Generate");

    ButtonSpec selected_tab = TabButtonSpec("Player", true);
    assert(selected_tab.role == ButtonRole::Tab);
    assert(selected_tab.selected);
    assert(ResolveButtonState(selected_tab, false) == ButtonState::Selected);

    ButtonSpec compact_danger = WithButtonSize(DangerButtonSpec("Delete"), ButtonSize::Compact);
    assert(compact_danger.role == ButtonRole::Danger);
    assert(compact_danger.size == ButtonSize::Compact);

    ButtonSpec disabled_utility = WithButtonEnabled(UtilityButtonSpec("Copy path"), false);
    assert(disabled_utility.role == ButtonRole::Utility);
    assert(!disabled_utility.enabled);

    ButtonSpec custom_variant = WithButtonVariant(SecondaryButtonSpec("Rename"),
                                                 ButtonVariant::Minimal);
    assert(custom_variant.variant == ButtonVariant::Minimal);

    ButtonSpec success_button = SuccessButtonSpec("Connected", "✓");
    assert(success_button.role == ButtonRole::Success);
    assert(success_button.variant == ButtonVariant::AccentBar);

    ButtonSpec navigation_button = NavigationButtonSpec("Parent", "↑");
    assert(navigation_button.role == ButtonRole::Navigation);

    ButtonSpec media_button = MediaButtonSpec("Play", "▶");
    assert(media_button.role == ButtonRole::Media);

    ButtonSpec disabled_button = save_button;
    disabled_button.enabled = false;
    assert(ResolveButtonState(disabled_button, true) == ButtonState::Disabled);

    RenderButton(theme, save_button, false);
    RenderButton(theme, save_button, true);
    RenderButton(theme, icon_button, false);
    RenderRoleButton(theme, ButtonRole::Primary, "Open", false);
    RenderRoleButton(theme, ButtonRole::Tab, "Run", false, "", ButtonSize::Normal, true);

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

    auto primary_button = MakePrimaryButton(&theme, "Save", [] {});
    assert(primary_button != nullptr);

    auto danger_button = MakeDangerButton(&theme, "Delete", [] {});
    assert(danger_button != nullptr);

    auto warning_button = MakeWarningButton(&theme, "Clear", [] {});
    assert(warning_button != nullptr);

    auto success_component = MakeSuccessButton(&theme, "Connected", [] {});
    assert(success_component != nullptr);

    auto cancel_button = MakeCancelButton(&theme, [] {});
    assert(cancel_button != nullptr);

    auto navigation_component = MakeNavigationButton(&theme, "Parent", [] {});
    assert(navigation_component != nullptr);

    auto media_component = MakeMediaButton(&theme, "Play", [] {}, "▶");
    assert(media_component != nullptr);

    (void)normal;
    (void)focused;
    (void)selected;
    (void)disabled;

    return 0;
}
