#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"

namespace textlt {

enum class ShortcutContext {
    Menu,
    Text,
};

enum class ShortcutKeyModifier {
    None,
    Ctrl,
    Alt,
    Shift,
    CtrlShift,
    AltShift,
    CtrlAlt,
};

struct ShortcutKey {
    ShortcutKeyModifier modifier = ShortcutKeyModifier::Ctrl;
    std::string key;

    bool empty() const { return key.empty(); }
};

std::string ShortcutContextName(ShortcutContext context);
std::string ShortcutContextStoragePrefix(ShortcutContext context);
std::string ShortcutModifierName(ShortcutKeyModifier modifier);
std::vector<ShortcutKeyModifier> ShortcutModifierChoices();
std::vector<std::string> ShortcutKeyChoices(ShortcutKeyModifier modifier);
std::optional<ShortcutKey> ParseShortcutKey(const std::string& shortcut);
std::string ShortcutKeyToString(const ShortcutKey& shortcut);
bool ShortcutKeysEqual(const ShortcutKey& left, const ShortcutKey& right);
bool ShortcutKeyMatchesEvent(const ShortcutKey& shortcut, const ftxui::Event& event);
bool IsTerminalReservedShortcut(const ShortcutKey& shortcut);

} // namespace textlt
