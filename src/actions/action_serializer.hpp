#pragma once

#include <filesystem>
#include <string>

#include "actions/action_model.hpp"

namespace textlt::actions {

class ActionSerializer {
public:
    static bool Load(const std::filesystem::path& path, ActionsDocument& document,
        std::string& error);
    static bool Save(const std::filesystem::path& path, const ActionsDocument& document,
        std::string& error);
};

} // namespace textlt::actions
