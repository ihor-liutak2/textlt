#include "piper_manager.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace textlt {
namespace {

std::filesystem::path UserHomeDirectory() {
#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && std::string(user_profile).empty() == false) {
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

void EnsureDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path, error);
}

std::filesystem::path DownloadCacheDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && std::string(local_app_data).empty() == false) {
        return std::filesystem::path(local_app_data) / "textlt" / "cache";
    }

    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / "AppData" / "Local" / "textlt" / "cache";
#else
    const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache_home && std::string(xdg_cache_home).empty() == false) {
        return std::filesystem::path(xdg_cache_home) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".cache" / "textlt";
#endif
}

} // namespace

std::filesystem::path PiperManager::UserDataDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && std::string(local_app_data).empty() == false) {
        return std::filesystem::path(local_app_data) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / "AppData" / "Local" / "textlt";
#else
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && std::string(xdg_data_home).empty() == false) {
        return std::filesystem::path(xdg_data_home) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".local" / "share" / "textlt";
#endif
}

std::filesystem::path PiperManager::RuntimeDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "piper" / "bin";
}

std::filesystem::path PiperManager::ModelsDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "piper" / "models";
}

std::filesystem::path PiperManager::RuntimeDownloadArchivePath() {
#ifdef _WIN32
    const std::string filename = "piper_windows_amd64.zip";
#else
    const std::string filename = "piper_linux_x86_64.tar.gz";
#endif
    const std::filesystem::path cache = DownloadCacheDirectory();
    return cache.empty() ? std::filesystem::path{} : cache / filename;
}

std::string PiperManager::RuntimeDownloadUrl() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip";
#elif !defined(_WIN32) && defined(__x86_64__)
    return "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz";
#else
    return {};
#endif
}

std::filesystem::path PiperManager::RuntimeExecutablePath() {
    const std::filesystem::path runtime_directory = RuntimeDirectory();
    if (runtime_directory.empty()) {
        return {};
    }
    std::error_code error;
    if (!std::filesystem::exists(runtime_directory, error)) {
        return {};
    }

#ifdef _WIN32
    constexpr const char* binary_name = "piper.exe";
#else
    constexpr const char* binary_name = "piper";
#endif

    std::filesystem::recursive_directory_iterator iterator(runtime_directory, error);
    std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(error) && entry.path().filename() == binary_name) {
            return entry.path();
        }
        error.clear();
        iterator.increment(error);
    }
    return {};
}

bool PiperManager::RuntimeInstalled() {
    return !RuntimeExecutablePath().empty();
}

bool PiperManager::VoiceInstalled(const Json& voice) {
    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    if (model_path.empty() || config_path.empty()) {
        return false;
    }

    const std::filesystem::path models_directory = ModelsDirectory();
    std::error_code error;
    return std::filesystem::exists(models_directory / model_path, error) &&
           std::filesystem::exists(models_directory / config_path, error);
}

bool PiperManager::FindVoiceById(const std::string& voice_id, Json* voice) {
    if (!voice || voice_id.empty()) {
        return false;
    }

    const std::filesystem::path registry_path =
        UserDataDirectory() / "registries" / "piper_voices_index.json";
    std::ifstream file(registry_path, std::ios::binary);
    if (!file) {
        return false;
    }

    const Json root = Json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    const auto voices = root.find("voices");
    if (voices == root.end() || !voices->is_array()) {
        return false;
    }

    for (const Json& candidate : *voices) {
        if (candidate.is_object() && JsonString(candidate, "id") == voice_id) {
            *voice = candidate;
            return true;
        }
    }
    return false;
}

bool PiperManager::RunToFile(const Json& voice,
                             const std::string& text,
                             const std::filesystem::path& output_wav,
                             std::string* error) {
    const std::filesystem::path executable = RuntimeExecutablePath();
    if (executable.empty()) {
        if (error) {
            *error = "Piper runtime is not installed";
        }
        return false;
    }

    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    const std::filesystem::path model = ModelsDirectory() / model_path;
    const std::filesystem::path config = ModelsDirectory() / config_path;
    std::error_code exists_error;
    if (!std::filesystem::exists(model, exists_error) ||
        !std::filesystem::exists(config, exists_error)) {
        if (error) {
            *error = "Selected Piper voice files are missing";
        }
        return false;
    }

    EnsureDirectory(output_wav.parent_path());
    const std::filesystem::path input_path = output_wav.string() + ".txt";
    {
        std::ofstream input(input_path, std::ios::binary | std::ios::trunc);
        if (!input) {
            if (error) {
                *error = "Cannot write Piper input text";
            }
            return false;
        }
        input << text << '\n';
    }

#ifdef _WIN32
    const std::string command =
        "type " + QuoteShellPath(input_path) +
        " | " + QuoteShellPath(executable) +
        " --model " + QuoteShellPath(model) +
        " --config " + QuoteShellPath(config) +
        " --output_file " + QuoteShellPath(output_wav);
#else
    const std::string command =
        QuoteShellPath(executable) +
        " --model " + QuoteShellPath(model) +
        " --config " + QuoteShellPath(config) +
        " --output_file " + QuoteShellPath(output_wav) +
        " < " + QuoteShellPath(input_path);
#endif
    const int result = std::system(command.c_str());
    std::error_code remove_error;
    std::filesystem::remove(input_path, remove_error);
    if (result != 0) {
        if (error) {
            *error = "Piper command failed";
        }
        return false;
    }
    if (!std::filesystem::exists(output_wav, exists_error)) {
        if (error) {
            *error = "Piper did not create audio file";
        }
        return false;
    }
    return true;
}

std::string PiperManager::QuoteShellPath(const std::filesystem::path& path) {
#ifdef _WIN32
    std::string value = path.string();
    std::string quoted = "\"";
    for (char character : value) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "\"";
    return quoted;
#else
    const std::string value = path.string();
    std::string quoted = "'";
    for (char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "'";
    return quoted;
#endif
}

std::string PiperManager::QuotePowerShellSingle(const std::filesystem::path& path) {
    std::string value = path.string();
    std::string quoted = "'";
    for (char character : value) {
        if (character == '\'') {
            quoted += "''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "'";
    return quoted;
}

} // namespace textlt
