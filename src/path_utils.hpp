#pragma once

#include <filesystem>
#include <string>

namespace textlt {

inline std::string Utf8FromPath(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

inline std::filesystem::path PathFromUtf8(const std::string& value) {
    return std::filesystem::u8path(value);
}
} // namespace textlt
