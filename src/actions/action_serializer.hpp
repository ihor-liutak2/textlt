#pragma once

#include <filesystem>
#include <string>

#include "actions/action_model.hpp"

namespace textlt::actions {

class ActionSerializer {
public:
    // migrated is set to true when a schema version 1 document was converted
    // to version 2 in memory. The caller is responsible for persisting it.
    static bool Load(const std::filesystem::path& path, ActionsDocument& document,
        std::string& error, bool* migrated = nullptr);
    static bool Save(const std::filesystem::path& path, const ActionsDocument& document,
        std::string& error);
};

} // namespace textlt::actions
