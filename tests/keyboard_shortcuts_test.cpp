#include "keyboard_shortcuts.hpp"

#include <cassert>

int main() {
    using textlt::MatchesPlainShortcutKey;
    using textlt::MatchesShortcut;
    using textlt::ShortcutModifier;

    assert(MatchesPlainShortcutKey(ftxui::Event::Character("w"), 'w'));
    assert(MatchesPlainShortcutKey(ftxui::Event::Character("W"), 'w'));
    assert(MatchesPlainShortcutKey(ftxui::Event::Character("ц"), 'w'));
    assert(MatchesPlainShortcutKey(ftxui::Event::Character("Ц"), 'w'));
    assert(MatchesPlainShortcutKey(ftxui::Event::Character("і"), 's'));
    assert(!MatchesPlainShortcutKey(ftxui::Event::Character("ц"), 'q'));

    assert(MatchesShortcut(ftxui::Event::Special("\x17"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1B[1094;5u"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1B[27;5;1094~"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1Bц"), ShortcutModifier::Alt, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1BЦ"), ShortcutModifier::Alt, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("Alt+ц"), ShortcutModifier::Alt, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("Alt+Ц"), ShortcutModifier::Alt, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1B[1062;3u"), ShortcutModifier::Alt, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("Ctrl+ц"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("\x1B[1062;5u"), ShortcutModifier::Ctrl, 'w'));
    assert(MatchesShortcut(ftxui::Event::Special("Ctrl+Alt+P"), ShortcutModifier::CtrlAlt, 'p'));
    assert(!MatchesShortcut(ftxui::Event::Character("ц"), ShortcutModifier::Ctrl, 'w'));
    assert(!MatchesShortcut(ftxui::Event::Special("\x1B[1094;5u"), ShortcutModifier::Ctrl, 'q'));
    return 0;
}
