#pragma once

#ifdef _WIN32

#include <filesystem>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace textlt {

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

} // namespace textlt

#endif
