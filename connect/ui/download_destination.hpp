#pragma once

#include <filesystem>

#include "textlt/connect/provider.hpp"

namespace textlt::connect_ui {

std::string SafeDownloadBasename(std::string name);

std::unique_ptr<connect::FileDestination> MakeDownloadDestination(
    const std::filesystem::path& downloads, std::string name);

} // namespace textlt::connect_ui
