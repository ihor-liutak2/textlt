#include "app_resources.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "editor_config.hpp"

namespace textlt {
namespace {

std::filesystem::path UserHomeDirectory() {
#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile);
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home);
#endif
}

std::filesystem::path UserCacheDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && !std::string(local_app_data).empty()) {
        return std::filesystem::path(local_app_data) / "textlt" / "cache";
    }

    const std::filesystem::path home = UserHomeDirectory();
    if (!home.empty()) {
        return home / "AppData" / "Local" / "textlt" / "cache";
    }
    return {};
#else
    const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache_home && !std::string(xdg_cache_home).empty()) {
        return std::filesystem::path(xdg_cache_home) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    if (!home.empty()) {
        return home / ".cache" / "textlt";
    }
    return {};
#endif
}

std::filesystem::path UserDataDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && !std::string(local_app_data).empty()) {
        return std::filesystem::path(local_app_data) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    if (!home.empty()) {
        return home / "AppData" / "Local" / "textlt";
    }
    return {};
#else
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && !std::string(xdg_data_home).empty()) {
        return std::filesystem::path(xdg_data_home) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    if (!home.empty()) {
        return home / ".local" / "share" / "textlt";
    }
    return {};
#endif
}

std::filesystem::path UserConfigDirectory() {
    return EditorConfig::DefaultConfigPath().parent_path();
}

void EnsureDirectoryExists(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(path, error);
}

void EnsureDefaultConfigFile() {
    const std::filesystem::path config_path = EditorConfig::DefaultConfigPath();
    if (config_path.empty()) {
        return;
    }

    std::error_code error;
    if (std::filesystem::exists(config_path, error)) {
        return;
    }

    EditorConfig config;
    config.SetConfigPath(config_path);
    config.Persist();
}

void EnsureAssistantDataDirectories() {
    const std::filesystem::path data_directory = UserDataDirectory();
    if (data_directory.empty()) {
        return;
    }

    EnsureDirectoryExists(data_directory / "piper" / "bin");
    EnsureDirectoryExists(data_directory / "piper" / "models");
    EnsureDirectoryExists(data_directory / "ai" / "runtimes");
    EnsureDirectoryExists(data_directory / "ai" / "models");
}

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    std::error_code error;
    const std::filesystem::path executable =
        std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path();
#endif
}

bool IsTextProcessorsDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(
        path / "default_text_parsers.json", error);
}

} // namespace

std::filesystem::path FindTextProcessorsDirectory() {
    std::vector<std::filesystem::path> candidates;

    if (const char* data_directory = std::getenv("TEXTLT_DATA_DIR")) {
        if (*data_directory != '\0') {
            const std::filesystem::path root(data_directory);
            candidates.push_back(root / "text_processors");
            candidates.push_back(root);
        }
    }

    if (const char* appdir = std::getenv("APPDIR")) {
        if (*appdir != '\0') {
            candidates.emplace_back(
                std::filesystem::path(appdir) / "usr" / "share" / "textlt" /
                "text_processors");
        }
    }

    const std::filesystem::path executable_directory = ExecutableDirectory();
    if (!executable_directory.empty()) {
        candidates.push_back(executable_directory / "text_processors");
        candidates.push_back(
            executable_directory.parent_path() / "share" / "textlt" /
            "text_processors");
    }
    candidates.push_back(std::filesystem::current_path() / "text_processors");

    for (const auto& candidate : candidates) {
        if (IsTextProcessorsDirectory(candidate)) {
            return candidate;
        }
    }
    return {};
}

void EnsureStartupResources() {
    const std::filesystem::path config_directory = UserConfigDirectory();
    if (!config_directory.empty()) {
        EnsureDirectoryExists(config_directory);
        EnsureDirectoryExists(config_directory / "themes");
    }
    EnsureDefaultConfigFile();
}

void EnsureAssistantResources() {
    const std::filesystem::path config_directory = UserConfigDirectory();
    if (!config_directory.empty()) {
        EnsureDirectoryExists(config_directory / "registries");
    }

    EnsureAssistantDataDirectories();
    EnsureDirectoryExists(UserCacheDirectory());
}

} // namespace textlt
