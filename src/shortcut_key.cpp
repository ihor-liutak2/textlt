#include "shortcut_key.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

#include "keyboard_shortcuts.hpp"

namespace textlt {
namespace {

std::string UppercaseAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return value;
}

bool EqualsIgnoreCase(const std::string& left, const std::string& right) {
    return UppercaseAscii(left) == UppercaseAscii(right);
}


std::string DisplayKeyName(const std::string& key) {
    const std::string upper = UppercaseAscii(key);
    if (upper == "/") return "Slash";
    if (upper == "ESCAPE") return "Escape";
    if (upper == "LEFT") return "Left";
    if (upper == "RIGHT") return "Right";
    if (upper == "UP") return "Up";
    if (upper == "DOWN") return "Down";
    if (upper == "HOME") return "Home";
    if (upper == "END") return "End";
    if (upper == "PAGEUP") return "PageUp";
    if (upper == "PAGEDOWN") return "PageDown";
    if (upper == "DELETE") return "Delete";
    if (upper == "BACKSPACE") return "Backspace";
    if (upper == "TAB") return "Tab";
    return upper;
}

bool IsLetterKey(const std::string& key) {
    return key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z';
}

bool IsDigitKey(const std::string& key) {
    return key.size() == 1 && key[0] >= '0' && key[0] <= '9';
}

bool IsFunctionKey(const std::string& key, int* number = nullptr) {
    if (key.size() < 2 || key[0] != 'F') {
        return false;
    }
    int value = 0;
    for (size_t index = 1; index < key.size(); ++index) {
        if (key[index] < '0' || key[index] > '9') {
            return false;
        }
        value = value * 10 + (key[index] - '0');
    }
    if (value < 1 || value > 12) {
        return false;
    }
    if (number) {
        *number = value;
    }
    return true;
}

bool IsNamedEvent(const ftxui::Event& event, const std::string& name) {
    return event.input() == name || event == ftxui::Event::Special(name);
}

bool MatchesFunctionKey(const std::string& key, const ftxui::Event& event) {
    int number = 0;
    if (!IsFunctionKey(key, &number)) {
        return false;
    }
    switch (number) {
        case 1: return event == ftxui::Event::F1;
        case 2: return event == ftxui::Event::F2;
        case 3: return event == ftxui::Event::F3;
        case 4: return event == ftxui::Event::F4;
        case 5: return event == ftxui::Event::F5;
        case 6: return event == ftxui::Event::F6;
        case 7: return event == ftxui::Event::F7;
        case 8: return event == ftxui::Event::F8;
        case 9: return event == ftxui::Event::F9;
        case 10: return event == ftxui::Event::F10;
        case 11: return event == ftxui::Event::F11;
        case 12: return event == ftxui::Event::F12;
    }
    return false;
}

bool MatchesSpecialInput(const ftxui::Event& event, const std::vector<std::string>& names) {
    const std::string& input = event.input();
    for (const std::string& name : names) {
        if (input == name || event == ftxui::Event::Special(name)) {
            return true;
        }
    }
    return false;
}

bool MatchesModifiedSpecialKey(const ShortcutKey& shortcut, const ftxui::Event& event) {
    const std::string key = UppercaseAscii(shortcut.key);
    const ShortcutKeyModifier modifier = shortcut.modifier;

    if (modifier == ShortcutKeyModifier::None && key == "ESCAPE") {
        return event == ftxui::Event::Escape || MatchesSpecialInput(event, {"Escape", "Esc"});
    }

    if (modifier == ShortcutKeyModifier::Ctrl && key == "LEFT") {
        return event == ftxui::Event::ArrowLeftCtrl ||
            MatchesSpecialInput(event, {"Ctrl+Left", "\x1B[1;5D", "\x1B[27;5;68~", "\x1B[68;5u"});
    }
    if (modifier == ShortcutKeyModifier::Ctrl && key == "RIGHT") {
        return event == ftxui::Event::ArrowRightCtrl ||
            MatchesSpecialInput(event, {"Ctrl+Right", "\x1B[1;5C", "\x1B[27;5;67~", "\x1B[67;5u"});
    }
    if (modifier == ShortcutKeyModifier::Ctrl && key == "UP") {
        return event == ftxui::Event::ArrowUpCtrl ||
            MatchesSpecialInput(event, {"Ctrl+Up", "\x1B[1;5A", "\x1B[27;5;65~", "\x1B[65;5u"});
    }
    if (modifier == ShortcutKeyModifier::Ctrl && key == "DOWN") {
        return event == ftxui::Event::ArrowDownCtrl ||
            MatchesSpecialInput(event, {"Ctrl+Down", "\x1B[1;5B", "\x1B[27;5;66~", "\x1B[66;5u"});
    }
    if (modifier == ShortcutKeyModifier::Ctrl && key == "HOME") {
        return MatchesSpecialInput(event, {"Ctrl+Home", "\x1B[1;5H", "\x1B[7;5~", "\x1B[1;5~", "\x1B[5H"});
    }
    if (modifier == ShortcutKeyModifier::Ctrl && key == "END") {
        return MatchesSpecialInput(event, {"Ctrl+End", "\x1B[1;5F", "\x1B[8;5~", "\x1B[4;5~", "\x1B[5F"});
    }
    if (modifier == ShortcutKeyModifier::Ctrl && key == "DELETE") {
        return MatchesSpecialInput(event, {"Ctrl+Delete", "\x1B[3;5~", "\x1B[3;5u", "\x1B[27;5;3~"});
    }
    if (modifier == ShortcutKeyModifier::Ctrl && key == "BACKSPACE") {
        return event.input() == "\x17" ||
            MatchesSpecialInput(event, {"Ctrl+Backspace", "\x1B[127;5u", "\x1B[8;5u", "\x1B[127;5~", "\x1B[8;5~", "\x1B[27;5;127~", "\x1B[27;5;8~", "\x17"});
    }

    if (modifier == ShortcutKeyModifier::Alt && key == "LEFT") {
        return MatchesSpecialInput(event, {"Alt+Left", "\x1B[1;3D", "\x1B[1;9D", "\x1B[27;3;68~", "\x1B[68;3u"});
    }
    if (modifier == ShortcutKeyModifier::Alt && key == "RIGHT") {
        return MatchesSpecialInput(event, {"Alt+Right", "\x1B[1;3C", "\x1B[1;9C", "\x1B[27;3;67~", "\x1B[67;3u"});
    }
    if (modifier == ShortcutKeyModifier::Alt && key == "UP") {
        return MatchesSpecialInput(event, {"Alt+Up", "\x1B[1;3A", "\x1B[1;9A", "\x1B[27;3;65~", "\x1B[65;3u"});
    }
    if (modifier == ShortcutKeyModifier::Alt && key == "DOWN") {
        return MatchesSpecialInput(event, {"Alt+Down", "\x1B[1;3B", "\x1B[1;9B", "\x1B[27;3;66~", "\x1B[66;3u"});
    }
    if (modifier == ShortcutKeyModifier::Alt && key == "BACKSPACE") {
        return MatchesSpecialInput(event, {"Alt+Backspace", "\x1B\x7F", "\x1B\x08"});
    }

    if (modifier == ShortcutKeyModifier::Shift && key == "LEFT") {
        return MatchesSpecialInput(event, {"Shift+Left", "\x1B[1;2D"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "RIGHT") {
        return MatchesSpecialInput(event, {"Shift+Right", "\x1B[1;2C"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "UP") {
        return MatchesSpecialInput(event, {"Shift+Up", "\x1B[1;2A"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "DOWN") {
        return MatchesSpecialInput(event, {"Shift+Down", "\x1B[1;2B"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "HOME") {
        return MatchesSpecialInput(event, {"Shift+Home", "\x1B[1;2H", "\x1B[7;2~", "\x1B[1;2~", "\x1B[2H"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "END") {
        return MatchesSpecialInput(event, {"Shift+End", "\x1B[1;2F", "\x1B[8;2~", "\x1B[4;2~", "\x1B[2F"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "PAGEUP") {
        return MatchesSpecialInput(event, {"Shift+PageUp", "\x1B[5;2~"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "PAGEDOWN") {
        return MatchesSpecialInput(event, {"Shift+PageDown", "\x1B[6;2~"});
    }
    if (modifier == ShortcutKeyModifier::Shift && key == "TAB") {
        return MatchesSpecialInput(event, {"Shift+Tab", "\x1B[Z", "\x1B[1;2Z"});
    }

    if (modifier == ShortcutKeyModifier::CtrlShift && key == "HOME") {
        return MatchesSpecialInput(event, {"Ctrl+Shift+Home", "Shift+Ctrl+Home", "\x1B[1;6H", "\x1B[7;6~", "\x1B[1;6~", "\x1B[6H"});
    }
    if (modifier == ShortcutKeyModifier::CtrlShift && key == "END") {
        return MatchesSpecialInput(event, {"Ctrl+Shift+End", "Shift+Ctrl+End", "\x1B[1;6F", "\x1B[8;6~", "\x1B[4;6~", "\x1B[6F"});
    }
    if (modifier == ShortcutKeyModifier::CtrlShift && key == "LEFT") {
        return MatchesSpecialInput(event, {"Ctrl+Shift+Left", "Shift+Ctrl+Left", "\x1B[1;6D", "\x1B[1;10D", "\x1B[27;6;68~", "\x1B[68;6u"});
    }
    if (modifier == ShortcutKeyModifier::CtrlShift && key == "RIGHT") {
        return MatchesSpecialInput(event, {"Ctrl+Shift+Right", "Shift+Ctrl+Right", "\x1B[1;6C", "\x1B[1;10C", "\x1B[27;6;67~", "\x1B[67;6u"});
    }
    if (modifier == ShortcutKeyModifier::CtrlShift && key == "UP") {
        return MatchesSpecialInput(event, {"Ctrl+Shift+Up", "Shift+Ctrl+Up", "\x1B[1;6A", "\x1B[1;10A", "\x1B[27;6;65~", "\x1B[65;6u"});
    }
    if (modifier == ShortcutKeyModifier::CtrlShift && key == "DOWN") {
        return MatchesSpecialInput(event, {"Ctrl+Shift+Down", "Shift+Ctrl+Down", "\x1B[1;6B", "\x1B[1;10B", "\x1B[27;6;66~", "\x1B[66;6u"});
    }

    if (modifier == ShortcutKeyModifier::AltShift && key == "DOWN") {
        return MatchesSpecialInput(event, {"Alt+Shift+Down", "Shift+Alt+Down", "\x1B[1;4B", "\x1B[1;10B", "\x1B[27;4;66~", "\x1B[66;4u"});
    }
    if (modifier == ShortcutKeyModifier::AltShift && key == "UP") {
        return MatchesSpecialInput(event, {"Alt+Shift+Up", "Shift+Alt+Up", "\x1B[1;4A", "\x1B[1;10A", "\x1B[27;4;65~", "\x1B[65;4u"});
    }

    return false;
}

} // namespace

std::string ShortcutContextName(ShortcutContext context) {
    return context == ShortcutContext::Menu ? "Menu" : "Text";
}

std::string ShortcutContextStoragePrefix(ShortcutContext context) {
    return context == ShortcutContext::Menu ? "menu" : "text";
}

std::string ShortcutModifierName(ShortcutKeyModifier modifier) {
    switch (modifier) {
        case ShortcutKeyModifier::None: return "None";
        case ShortcutKeyModifier::Ctrl: return "Ctrl";
        case ShortcutKeyModifier::Alt: return "Alt";
        case ShortcutKeyModifier::Shift: return "Shift";
        case ShortcutKeyModifier::CtrlShift: return "Ctrl+Shift";
        case ShortcutKeyModifier::AltShift: return "Alt+Shift";
        case ShortcutKeyModifier::CtrlAlt: return "Ctrl+Alt";
    }
    return "Ctrl";
}

std::vector<ShortcutKeyModifier> ShortcutModifierChoices() {
    return {
        ShortcutKeyModifier::None,
        ShortcutKeyModifier::Ctrl,
        ShortcutKeyModifier::Alt,
        ShortcutKeyModifier::Shift,
        ShortcutKeyModifier::CtrlShift,
        ShortcutKeyModifier::AltShift,
    };
}

std::vector<std::string> ShortcutKeyChoices(ShortcutKeyModifier modifier) {
    if (modifier == ShortcutKeyModifier::None) {
        return {"Escape"};
    }
    if (modifier == ShortcutKeyModifier::Shift) {
        return {"Left", "Right", "Up", "Down", "Home", "End", "PageUp", "PageDown", "Tab"};
    }
    std::vector<std::string> keys;
    keys.reserve(48);
    for (char key = 'A'; key <= 'Z'; ++key) {
        keys.emplace_back(1, key);
    }
    for (char key = '0'; key <= '9'; ++key) {
        keys.emplace_back(1, key);
    }
    keys.push_back("Slash");
    keys.push_back("Left");
    keys.push_back("Right");
    keys.push_back("Up");
    keys.push_back("Down");
    keys.push_back("Home");
    keys.push_back("End");
    keys.push_back("Delete");
    keys.push_back("Backspace");
    keys.push_back("Tab");
    for (int number = 1; number <= 12; ++number) {
        keys.push_back("F" + std::to_string(number));
    }

    keys.erase(std::remove_if(keys.begin(), keys.end(), [modifier](const std::string& key) {
        ShortcutKey shortcut;
        shortcut.modifier = modifier;
        shortcut.key = key == "Slash" ? "/" : key;
        return IsTerminalReservedShortcut(shortcut);
    }), keys.end());
    return keys;
}

std::optional<ShortcutKey> ParseShortcutKey(const std::string& shortcut) {
    if (shortcut.empty() || shortcut == "-") {
        return std::nullopt;
    }

    std::string normalized = shortcut;
    normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](char ch) {
        return ch == ' ';
    }), normalized.end());

    if (EqualsIgnoreCase(normalized, "Escape") || EqualsIgnoreCase(normalized, "Esc")) {
        return ShortcutKey{ShortcutKeyModifier::None, "ESCAPE"};
    }

    const std::vector<std::pair<std::string, ShortcutKeyModifier>> prefixes = {
        {"Ctrl+Shift+", ShortcutKeyModifier::CtrlShift},
        {"Shift+Ctrl+", ShortcutKeyModifier::CtrlShift},
        {"Alt+Shift+", ShortcutKeyModifier::AltShift},
        {"Shift+Alt+", ShortcutKeyModifier::AltShift},
        {"Ctrl+Alt+", ShortcutKeyModifier::CtrlAlt},
        {"Alt+Ctrl+", ShortcutKeyModifier::CtrlAlt},
        {"Ctrl+", ShortcutKeyModifier::Ctrl},
        {"Alt+", ShortcutKeyModifier::Alt},
        {"Shift+", ShortcutKeyModifier::Shift},
    };

    for (const auto& prefix : prefixes) {
        if (normalized.rfind(prefix.first, 0) == 0) {
            ShortcutKey result;
            result.modifier = prefix.second;
            result.key = normalized.substr(prefix.first.size());
            if (EqualsIgnoreCase(result.key, "Slash")) {
                result.key = "/";
            } else {
                result.key = UppercaseAscii(result.key);
            }
            if (result.key.empty()) {
                return std::nullopt;
            }
            return result;
        }
    }

    return std::nullopt;
}

std::string ShortcutKeyToString(const ShortcutKey& shortcut) {
    if (shortcut.empty()) {
        return "";
    }
    const std::string key = DisplayKeyName(shortcut.key);
    return shortcut.modifier == ShortcutKeyModifier::None
        ? key
        : ShortcutModifierName(shortcut.modifier) + "+" + key;
}

bool ShortcutKeysEqual(const ShortcutKey& left, const ShortcutKey& right) {
    return left.modifier == right.modifier && UppercaseAscii(left.key) == UppercaseAscii(right.key);
}

bool ShortcutKeyMatchesEvent(const ShortcutKey& shortcut, const ftxui::Event& event) {
    if (shortcut.empty() || event.is_mouse() || event.is_cursor_position()) {
        return false;
    }

    const std::string key = UppercaseAscii(shortcut.key);
    if (shortcut.modifier == ShortcutKeyModifier::None) {
        return MatchesModifiedSpecialKey(shortcut, event);
    }
    if (IsLetterKey(key)) {
        const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(key[0])));
        if (shortcut.modifier == ShortcutKeyModifier::Ctrl) {
            return MatchesShortcut(event, ShortcutModifier::Ctrl, letter);
        }
        if (shortcut.modifier == ShortcutKeyModifier::Alt) {
            return MatchesShortcut(event, ShortcutModifier::Alt, letter);
        }
        const std::string name = ShortcutKeyToString({shortcut.modifier, key});
        return IsNamedEvent(event, name) ||
            IsNamedEvent(event, "Shift+Ctrl+" + key) ||
            IsNamedEvent(event, "Shift+Alt+" + key) ||
            IsNamedEvent(event, "Alt+Ctrl+" + key);
    }

    if (key == "/") {
        if (shortcut.modifier == ShortcutKeyModifier::Ctrl) {
            return MatchesShortcut(event, ShortcutModifier::Ctrl, '/');
        }
        return IsNamedEvent(event, ShortcutKeyToString(shortcut));
    }

    if (IsDigitKey(key)) {
        const std::string name = ShortcutKeyToString({shortcut.modifier, key});
        if (IsNamedEvent(event, name)) {
            return true;
        }
        if (shortcut.modifier == ShortcutKeyModifier::Ctrl) {
            const std::string code = std::to_string(static_cast<int>(key[0]));
            return event.input() == "\x1B[" + code + ";5u" ||
                event.input() == "\x1B[27;5;" + code + "~";
        }
        return false;
    }

    if (shortcut.modifier == ShortcutKeyModifier::Ctrl && MatchesFunctionKey(key, event)) {
        return true;
    }
    if (MatchesFunctionKey(key, event)) {
        return IsNamedEvent(event, ShortcutKeyToString(shortcut));
    }

    return MatchesModifiedSpecialKey(shortcut, event);
}

bool IsTerminalReservedShortcut(const ShortcutKey& shortcut) {
    const std::string key = UppercaseAscii(shortcut.key);
    if (shortcut.modifier != ShortcutKeyModifier::Ctrl) {
        return false;
    }

    static const std::unordered_set<std::string> reserved = {
        "D",      // EOF in many shells.
        "I",      // Tab.
        "J",      // Line feed.
        "M",      // Return.
        "[",      // Escape.
        "\\",    // Quit signal in canonical terminals.
        "]",
        "@",
        "SPACE",
    };
    return reserved.find(key) != reserved.end();
}

} // namespace textlt
