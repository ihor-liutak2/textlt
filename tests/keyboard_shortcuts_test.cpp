#include "keyboard_shortcuts.hpp"

#include <cassert>

int main() {
    using textlt::MatchesShortcut;
    using textlt::ShortcutModifier;

    assert(MatchesShortcut(ftxui::Event::Special("\x17"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1B[1094;5u"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1B[27;5;1094~"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1Bц"), ShortcutModifier::Alt, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("Ctrl+Alt+P"), ShortcutModifier::CtrlAlt, 'p'));
    assert(!MatchesShortcut(ftxui::Event::Character("ц"), ShortcutModifier::Ctrl, 'w'));
    assert(!MatchesShortcut(ftxui::Event::Special("\x1B[1094;5u"), ShortcutModifier::Ctrl, 'q'));
    return 0;
}
