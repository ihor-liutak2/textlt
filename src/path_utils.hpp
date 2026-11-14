#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace textlt {

inline std::string Utf8FromPath(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

inline std::filesystem::path PathFromUtf8(const std::string& value) {
    return std::filesystem::u8path(value);
}

#ifdef _WIN32
inline std::filesystem::path WindowsEnvironmentPath(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return {};
    }

    std::wstring value(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), length);
    if (written == 0 || written >= length) {
        return {};
    }
    value.resize(written);
    return std::filesystem::path(value);
}
#endif

} // namespace textlt
