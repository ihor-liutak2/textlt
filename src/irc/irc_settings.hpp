#pragma once

#include <filesystem>
#include <vector>

#include "textlt/connect/irc_provider.hpp"

namespace textlt::irc {

std::vector<connect::IrcProfile> LoadSettings(const std::filesystem::path& path);
bool SaveSettings(const std::filesystem::path& path, const std::vector<connect::IrcProfile>& profiles);
std::string NewProfileId();

} // namespace textlt::irc
