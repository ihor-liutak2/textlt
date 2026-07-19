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
    registry.RegisterDefault({ShortcutContext::Text, "editor.document_start", "Document start", "Navigation", "Ctrl+Home"});
    registry.RegisterDefault({ShortcutContext::Text, "editor.select_line_start", "Select line start", "Selection", "Shift+Home"});
    registry.RegisterDefault({ShortcutContext::Text, "editor.select_page_down", "Select page down", "Selection", "Shift+PageDown"});
    registry.RegisterDefault({ShortcutContext::Text, "editor.select_document_end", "Select document end", "Selection", "Ctrl+Shift+End"});
    registry.RegisterDefault({ShortcutContext::Text, "editor.clear_selection", "Clear selection", "Selection", "Escape"});

    assert(registry.EffectiveShortcut(ShortcutContext::Menu, "file.save") == "Ctrl+S");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("Ctrl+S")) == "file.save");
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("Alt+Up")) == "editor.move_line_up");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("Alt+M")) == "ai.quick_actions");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("\x1Bm")) == "ai.quick_actions");
    assert(registry.MatchAction(ShortcutContext::Menu, ftxui::Event::Special("\x1Bь")) == "ai.quick_actions");
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("Ctrl+Home")) == "editor.document_start");
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("\x1B[1;2H")) == "editor.select_line_start");
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("\x1B[6;2~")) == "editor.select_page_down");
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("\x1B[1;6F")) == "editor.select_document_end");
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Escape) == "editor.clear_selection");

    std::string error;
    assert(!registry.SetOverride(ShortcutContext::Text, "editor.move_line_up", "Ctrl+S", error));
    assert(error.find("already assigned") != std::string::npos);

    assert(registry.SetOverride(ShortcutContext::Text, "editor.move_line_up", "Alt+Down", error));
    assert(registry.MatchAction(ShortcutContext::Text, ftxui::Event::Special("Alt+Down")) == "editor.move_line_up");

    ShortcutKey reserved{ShortcutKeyModifier::Ctrl, "D"};
    assert(IsTerminalReservedShortcut(reserved));

    const auto modifier_choices = ShortcutModifierChoices();
    assert(std::find(modifier_choices.begin(), modifier_choices.end(), ShortcutKeyModifier::None) !=
           modifier_choices.end());
    assert(std::find(modifier_choices.begin(), modifier_choices.end(), ShortcutKeyModifier::Shift) !=
           modifier_choices.end());
    assert(std::find(modifier_choices.begin(), modifier_choices.end(), ShortcutKeyModifier::CtrlAlt) ==
           modifier_choices.end());
    const auto shift = ParseShortcutKey("Shift+Left");
    assert(shift && shift->modifier == ShortcutKeyModifier::Shift);
    const auto escape = ParseShortcutKey("Escape");
    assert(escape && escape->modifier == ShortcutKeyModifier::None);
    assert(ShortcutKeyToString(*escape) == "Escape");

    assert(!registry.SetOverride(ShortcutContext::Menu, "file.save", "Escape", error));
    assert(error.find("Only Escape") != std::string::npos);
    assert(!registry.SetOverride(ShortcutContext::Menu, "file.save", "Shift+Home", error));
    assert(error.find("only for text-selection") != std::string::npos);
    assert(!registry.SetOverride(ShortcutContext::Text, "editor.move_line_up", "Shift+A", error));
    assert(error.find("Shift-only shortcuts") != std::string::npos);
    assert(!registry.SetOverride(ShortcutContext::Text, "editor.move_line_up", "Ctrl+Alt+P", error));
    assert(error.find("Ctrl+Alt") != std::string::npos);

    return 0;
}
