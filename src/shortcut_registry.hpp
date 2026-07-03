#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "shortcut_key.hpp"

namespace textlt {

struct ShortcutBindingDefinition {
    ShortcutContext context = ShortcutContext::Menu;
    std::string action_id;
    std::string title;
    std::string category;
    std::string default_shortcut;
};

struct ShortcutBindingView {
    ShortcutBindingDefinition definition;
    std::string effective_shortcut;
    std::string override_shortcut;
    bool custom = false;
};

struct ShortcutConflict {
    bool exists = false;
    ShortcutContext context = ShortcutContext::Menu;
    std::string action_id;
    std::string title;
    std::string shortcut;
};

class ShortcutRegistry {
public:
    void RegisterDefault(ShortcutBindingDefinition definition);
    void SetOverrides(std::unordered_map<std::string, std::string> overrides);
    const std::unordered_map<std::string, std::string>& Overrides() const { return overrides_; }

    std::vector<ShortcutBindingView> Bindings(ShortcutContext context) const;
    std::optional<ShortcutBindingView> Binding(ShortcutContext context, const std::string& action_id) const;
    std::string EffectiveShortcut(ShortcutContext context, const std::string& action_id) const;
    bool SetOverride(ShortcutContext context, const std::string& action_id, const std::string& shortcut, std::string& error);
    bool ClearOverride(ShortcutContext context, const std::string& action_id);
    void ClearAllOverrides();
    ShortcutConflict FindConflict(ShortcutContext context, const std::string& action_id, const std::string& shortcut) const;
    std::string MatchAction(ShortcutContext context, const ftxui::Event& event) const;

private:
    static std::string StorageKey(ShortcutContext context, const std::string& action_id);
    const ShortcutBindingDefinition* FindDefinition(ShortcutContext context, const std::string& action_id) const;

    std::vector<ShortcutBindingDefinition> definitions_;
    std::unordered_map<std::string, std::string> overrides_;
};

} // namespace textlt
