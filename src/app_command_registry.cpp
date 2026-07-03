#include "app_command_registry.hpp"

#include <algorithm>
#include <utility>

namespace textlt {

bool AppCommandRegistry::Register(AppCommand command) {
    if (command.id.empty() || !command.run) {
        return false;
    }

    auto result = commands_.emplace(command.id, std::move(command));
    return result.second;
}

bool AppCommandRegistry::Has(const std::string& id) const {
    return commands_.find(id) != commands_.end();
}

bool AppCommandRegistry::Enabled(const std::string& id) const {
    const AppCommand* command = Find(id);
    if (!command) {
        return false;
    }
    return !command->enabled || command->enabled();
}

bool AppCommandRegistry::Run(const std::string& id) const {
    const AppCommand* command = Find(id);
    if (!command || !command->run) {
        return false;
    }
    if (command->enabled && !command->enabled()) {
        return false;
    }
    command->run();
    return true;
}

const AppCommand* AppCommandRegistry::Find(const std::string& id) const {
    auto it = commands_.find(id);
    if (it == commands_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<AppCommand> AppCommandRegistry::Commands() const {
    std::vector<AppCommand> result;
    result.reserve(commands_.size());
    for (const auto& item : commands_) {
        result.push_back(item.second);
    }
    std::sort(result.begin(), result.end(), [](const AppCommand& left, const AppCommand& right) {
        if (left.category != right.category) {
            return left.category < right.category;
        }
        return left.id < right.id;
    });
    return result;
}

} // namespace textlt
