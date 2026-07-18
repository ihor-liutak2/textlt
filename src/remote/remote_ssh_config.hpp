#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

std::filesystem::path DefaultSshConfigPath();
std::vector<std::string> DiscoverSshConfigHosts(
    const std::filesystem::path& config_path,
    std::string& error);

} // namespace textlt
