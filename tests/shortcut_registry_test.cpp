#include "shortcut_registry.hpp"

#include <algorithm>
#include <cassert>
#include <string>

#include "ftxui/component/event.hpp"

int main() {
    using namespace textlt;

    ShortcutRegistry registry;
    registry.RegisterDefault({ShortcutContext::Menu, "file.save", "Save", "File", "Ctrl+S"});
    registry.RegisterDefault({ShortcutContext::Text, "editor.move_line_up", "Move line up", "Lines", "Alt+Up"});
    registry.RegisterDefault({ShortcutContext::Menu, "ai.quick_actions", "Quick AI", "AI", "Alt+M"});

    assert(registry.EffectiveShortcut(ShortcutContext::Menu, "file.save") == "Ctrl+S");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("Ctrl+S")) == "file.save");
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("Alt+Up")) == "editor.move_line_up");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("Alt+M")) == "ai.quick_actions");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("\x1Bm")) == "ai.quick_actions");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("\x1Bь")) == "ai.quick_actions");

    std::string error;
    assert(!registry.SetOverride(ShortcutContext::Text, "editor.move_line_up", "Ctrl+S", error));
    assert(error.find("already assigned") != std::string::npos);

    assert(registry.SetOverride(ShortcutContext::Text, "editor.move_line_up", "Alt+Down", error));
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("Alt+Down")) == "editor.move_line_up");

    ShortcutKey reserved{ShortcutKeyModifier::Ctrl, "D"};
    assert(IsTerminalReservedShortcut(reserved));

    const auto modifier_choices = ShortcutModifierChoices();
    assert(std::find(modifier_choices.begin(), modifier_choices.end(), ShortcutKeyModifier::Shift) ==
           modifier_choices.end());
    assert(std::find(modifier_choices.begin(), modifier_choices.end(), ShortcutKeyModifier::CtrlAlt) ==
           modifier_choices.end());
    // Existing Shift-only bindings remain readable even though the picker no
    // longer proposes creating new ones.
    const auto legacy_shift = ParseShortcutKey("Shift+Left");
    assert(legacy_shift && legacy_shift->modifier == ShortcutKeyModifier::Shift);

    assert(!registry.SetOverride(ShortcutContext::Text, "editor.move_line_up", "Ctrl+Alt+P", error));
    assert(error.find("Ctrl+Alt") != std::string::npos);

    return 0;
}
