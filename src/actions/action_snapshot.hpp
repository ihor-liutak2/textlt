#pragma once

#include <string_view>

#include "actions/action_model.hpp"

namespace textlt::actions {

// The JSON body shared by Connect action snapshots and clipboard transfers.
std::string EncodeActionSnapshot(const ActionItem& action);
std::optional<ActionItem> DecodeActionSnapshot(std::string_view json);

} // namespace textlt::actions
