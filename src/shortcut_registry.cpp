#include "shortcut_registry.hpp"

#include <algorithm>
#include <utility>

namespace textlt {

void ShortcutRegistry::RegisterDefault(ShortcutBindingDefinition definition) {
    if (definition.action_id.empty()) {
        return;
    }
    const auto exists = std::find_if(definitions_.begin(), definitions_.end(), [&](const ShortcutBindingDefinition& item) {
        return item.context == definition.context && item.action_id == definition.action_id;
    });
    if (exists != definitions_.end()) {
        *exists = std::move(definition);
        return;
    }
    definitions_.push_back(std::move(definition));
}

void ShortcutRegistry::SetOverrides(std::unordered_map<std::string, std::string> overrides) {
    overrides_ = std::move(overrides);
}

std::vector<ShortcutBindingView> ShortcutRegistry::Bindings(ShortcutContext context) const {
    std::vector<ShortcutBindingView> result;
    for (const ShortcutBindingDefinition& definition : definitions_) {
        if (definition.context != context) {
            continue;
        }
        ShortcutBindingView view;
        view.definition = definition;
        const std::string storage_key = StorageKey(context, definition.action_id);
        const auto override_it = overrides_.find(storage_key);
        if (override_it != overrides_.end()) {
            view.override_shortcut = override_it->second;
            view.effective_shortcut = override_it->second;
            view.custom = true;
        } else {
            view.effective_shortcut = definition.default_shortcut;
        }
        result.push_back(std::move(view));
    }
    std::sort(result.begin(), result.end(), [](const ShortcutBindingView& left, const ShortcutBindingView& right) {
        if (left.definition.category != right.definition.category) {
            return left.definition.category < right.definition.category;
        }
        return left.definition.title < right.definition.title;
    });
    return result;
}

std::optional<ShortcutBindingView> ShortcutRegistry::Binding(ShortcutContext context, const std::string& action_id) const {
    const ShortcutBindingDefinition* definition = FindDefinition(context, action_id);
    if (!definition) {
        return std::nullopt;
    }
    ShortcutBindingView view;
    view.definition = *definition;
    const std::string storage_key = StorageKey(context, action_id);
    const auto override_it = overrides_.find(storage_key);
    if (override_it != overrides_.end()) {
        view.override_shortcut = override_it->second;
        view.effective_shortcut = override_it->second;
        view.custom = true;
    } else {
        view.effective_shortcut = definition->default_shortcut;
    }
    return view;
}

std::string ShortcutRegistry::EffectiveShortcut(ShortcutContext context, const std::string& action_id) const {
    std::optional<ShortcutBindingView> view = Binding(context, action_id);
    return view ? view->effective_shortcut : std::string();
}

bool ShortcutRegistry::SetOverride(ShortcutContext context, const std::string& action_id, const std::string& shortcut, std::string& error) {
    if (!FindDefinition(context, action_id)) {
        error = "Unknown shortcut action: " + action_id;
        return false;
    }
    const auto parsed = ParseShortcutKey(shortcut);
    if (!parsed) {
        error = "Invalid shortcut: " + shortcut;
        return false;
    }
    if (parsed->modifier == ShortcutKeyModifier::CtrlAlt) {
        error = "Ctrl+Alt shortcuts are not supported because many terminals reserve or drop them.";
        return false;
    }
    if (IsTerminalReservedShortcut(*parsed)) {
        error = shortcut + " is reserved by terminals and cannot be assigned.";
        return false;
    }
    const ShortcutConflict conflict = FindConflict(context, action_id, shortcut);
    if (conflict.exists) {
        error = shortcut + " is already assigned to " + ShortcutContextName(conflict.context) + " / " + conflict.title + ".";
        return false;
    }
    overrides_[StorageKey(context, action_id)] = ShortcutKeyToString(*parsed);
    error.clear();
    return true;
}

bool ShortcutRegistry::ClearOverride(ShortcutContext context, const std::string& action_id) {
    return overrides_.erase(StorageKey(context, action_id)) > 0;
}

void ShortcutRegistry::ClearAllOverrides() {
    overrides_.clear();
}

ShortcutConflict ShortcutRegistry::FindConflict(ShortcutContext context, const std::string& action_id, const std::string& shortcut) const {
    const auto parsed = ParseShortcutKey(shortcut);
    if (!parsed) {
        return {};
    }

    for (const ShortcutBindingDefinition& definition : definitions_) {
        if (definition.context == context && definition.action_id == action_id) {
            continue;
        }
        const std::string candidate = EffectiveShortcut(definition.context, definition.action_id);
        const auto candidate_parsed = ParseShortcutKey(candidate);
        if (!candidate_parsed) {
            continue;
        }
        if (!ShortcutKeysEqual(*parsed, *candidate_parsed)) {
            continue;
        }
        ShortcutConflict conflict;
        conflict.exists = true;
        conflict.context = definition.context;
        conflict.action_id = definition.action_id;
        conflict.title = definition.title;
        conflict.shortcut = ShortcutKeyToString(*candidate_parsed);
        return conflict;
    }

    return {};
}

std::string ShortcutRegistry::MatchAction(ShortcutContext context, const ftxui::Event& event) const {
    for (const ShortcutBindingDefinition& definition : definitions_) {
        if (definition.context != context) {
            continue;
        }
        const auto parsed = ParseShortcutKey(EffectiveShortcut(context, definition.action_id));
        if (!parsed) {
            continue;
        }
        if (ShortcutKeyMatchesEvent(*parsed, event)) {
            return definition.action_id;
        }
    }
    return {};
}

std::string ShortcutRegistry::StorageKey(ShortcutContext context, const std::string& action_id) {
    return ShortcutContextStoragePrefix(context) + ":" + action_id;
}

const ShortcutBindingDefinition* ShortcutRegistry::FindDefinition(ShortcutContext context, const std::string& action_id) const {
    const auto it = std::find_if(definitions_.begin(), definitions_.end(), [&](const ShortcutBindingDefinition& item) {
        return item.context == context && item.action_id == action_id;
    });
    return it == definitions_.end() ? nullptr : &*it;
}

} // namespace textlt
