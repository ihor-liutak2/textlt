#include "app_resources.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

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

} // namespace

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
