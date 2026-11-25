#pragma once

#include <filesystem>
#include <optional>

namespace textlt::connect::detail {

// Creates a private combined certificate and key PEM if the file does not yet exist.
bool EnsureIdentityFile(const std::filesystem::path& path, const char* common_name);
std::optional<std::filesystem::path> CreateIdentityFile(const std::filesystem::path& directory,
                                                        const char* common_name);

} // namespace textlt::connect::detail
