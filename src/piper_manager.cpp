#include "piper_manager.hpp"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace textlt {
namespace {

std::optional<std::string> GetEnvValue(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return std::nullopt;
    }

    std::string value(buffer);
    free(buffer);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::filesystem::path UserHomeDirectory() {
    const std::optional<std::string> home = GetEnvValue(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    return home ? std::filesystem::path(*home) : std::filesystem::path{};
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
    const std::optional<std::string> local_app_data = GetEnvValue("LOCALAPPDATA");
    if (local_app_data) {
        return std::filesystem::path(*local_app_data) / "textlt" / "cache";
    }

    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / "AppData" / "Local" / "textlt" / "cache";
#else
    const std::optional<std::string> xdg_cache_home = GetEnvValue("XDG_CACHE_HOME");
    if (xdg_cache_home) {
        return std::filesystem::path(*xdg_cache_home) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".cache" / "textlt";
#endif
}

std::string RuntimeDownloadUrlFromRegistry() {
    const std::filesystem::path registry_path =
        PiperManager::UserDataDirectory() / "registries" / "piper_voices_index.json";
    std::error_code error;
    if (!std::filesystem::exists(registry_path, error) || error) {
        return {};
    }

    const Json registry = LoadJsonObject(registry_path);
    if (!registry.is_object()) {
        return {};
    }

    const Json urls = registry.contains("runtime_download_urls")
        ? registry["runtime_download_urls"]
        : Json::object();
    if (!urls.is_object()) {
        return {};
    }

#if defined(_WIN32)
    return JsonString(urls, "windows_amd64");
#elif defined(__x86_64__)
    return JsonString(urls, "linux_x86_64");
#else
    return {};
#endif
}

std::filesystem::path AssetPathFromUrlImpl(const std::string& url) {
    const std::string::size_type scheme_end = url.find("://");
    const std::string::size_type path_start = scheme_end == std::string::npos
        ? url.find('/')
        : url.find('/', scheme_end + 3);
    if (path_start == std::string::npos || path_start + 1 >= url.size()) {
        return {};
    }

    std::string relative = url.substr(path_start + 1);
    const std::string::size_type query = relative.find_first_of("?#");
    if (query != std::string::npos) {
        relative.erase(query);
    }
    while (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    return relative.empty() ? std::filesystem::path{} : std::filesystem::path(relative);
}

std::string SafeVoiceDirectoryName(const std::string& value) {
    std::string safe;
    safe.reserve(value.size());
    for (char character : value) {
        const bool keep =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' ||
            character == '_' ||
            character == '.';
        safe.push_back(keep ? character : '_');
    }
    while (!safe.empty() && safe.front() == '.') {
        safe.erase(safe.begin());
    }
    return safe;
}

std::filesystem::path VoiceDirectoryNameFromRegistry(const Json& voice) {
    std::string voice_id = JsonString(voice, "id");
    if (voice_id.empty()) {
        const std::filesystem::path model_path =
            AssetPathFromUrlImpl(JsonString(voice, "model_url"));
        voice_id = model_path.stem().string();
    }
    const std::string safe = SafeVoiceDirectoryName(voice_id);
    return safe.empty() ? std::filesystem::path{} : std::filesystem::path(safe);
}

std::filesystem::path VoiceAssetTargetPath(const Json& voice, const char* url_key) {
    const std::filesystem::path models_directory = PiperManager::ModelsDirectory();
    const std::filesystem::path voice_directory_name = VoiceDirectoryNameFromRegistry(voice);
    const std::filesystem::path asset_path = AssetPathFromUrlImpl(JsonString(voice, url_key));
    if (models_directory.empty() || voice_directory_name.empty() || asset_path.empty()) {
        return {};
    }
    return models_directory / voice_directory_name / asset_path.filename();
}

std::filesystem::path LegacyVoiceAssetPath(const Json& voice, const char* url_key) {
    const std::filesystem::path models_directory = PiperManager::ModelsDirectory();
    const std::filesystem::path asset_path = AssetPathFromUrlImpl(JsonString(voice, url_key));
    if (models_directory.empty() || asset_path.empty()) {
        return {};
    }
    return models_directory / asset_path;
}

std::vector<std::filesystem::path> UniqueVoicePathCandidates(
    const std::filesystem::path& primary,
    const std::filesystem::path& legacy) {
    std::vector<std::filesystem::path> candidates;
    if (!primary.empty()) {
        candidates.push_back(primary);
    }
    if (!legacy.empty()) {
        bool duplicate = false;
        for (const std::filesystem::path& candidate : candidates) {
            if (candidate == legacy) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            candidates.push_back(legacy);
        }
    }
    return candidates;
}

std::filesystem::path FirstExistingPath(const std::vector<std::filesystem::path>& candidates) {
    std::error_code error;
    for (const std::filesystem::path& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate, error) && !error) {
            return candidate;
        }
        error.clear();
    }
    return {};
}

} // namespace

std::filesystem::path PiperManager::UserDataDirectory() {
#ifdef _WIN32
    const std::optional<std::string> local_app_data = GetEnvValue("LOCALAPPDATA");
    if (local_app_data) {
        return std::filesystem::path(*local_app_data) / "textlt";
    }

    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / "AppData" / "Local" / "textlt";
#else
    const std::optional<std::string> xdg_data_home = GetEnvValue("XDG_DATA_HOME");
    if (xdg_data_home) {
        return std::filesystem::path(*xdg_data_home) / "textlt";
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
    return RuntimeDownloadUrlFromRegistry();
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

std::filesystem::path PiperManager::AssetPathFromUrl(const std::string& url) {
    return AssetPathFromUrlImpl(url);
}

std::filesystem::path PiperManager::VoiceDirectory(const Json& voice) {
    const std::filesystem::path models_directory = ModelsDirectory();
    const std::filesystem::path voice_directory_name = VoiceDirectoryNameFromRegistry(voice);
    if (models_directory.empty() || voice_directory_name.empty()) {
        return {};
    }
    return models_directory / voice_directory_name;
}

std::filesystem::path PiperManager::VoiceModelPath(const Json& voice) {
    return VoiceAssetTargetPath(voice, "model_url");
}

std::filesystem::path PiperManager::VoiceConfigPath(const Json& voice) {
    return VoiceAssetTargetPath(voice, "config_url");
}

std::vector<std::filesystem::path> PiperManager::VoiceModelPathCandidates(const Json& voice) {
    return UniqueVoicePathCandidates(
        VoiceModelPath(voice),
        LegacyVoiceAssetPath(voice, "model_url"));
}

std::vector<std::filesystem::path> PiperManager::VoiceConfigPathCandidates(const Json& voice) {
    return UniqueVoicePathCandidates(
        VoiceConfigPath(voice),
        LegacyVoiceAssetPath(voice, "config_url"));
}

bool PiperManager::VoiceInstalled(const Json& voice) {
    const std::filesystem::path model_path = FirstExistingPath(VoiceModelPathCandidates(voice));
    const std::filesystem::path config_path = FirstExistingPath(VoiceConfigPathCandidates(voice));
    return !model_path.empty() && !config_path.empty();
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

    const std::filesystem::path model = FirstExistingPath(VoiceModelPathCandidates(voice));
    const std::filesystem::path config = FirstExistingPath(VoiceConfigPathCandidates(voice));
    if (model.empty() || config.empty()) {
        if (error) {
            *error = "Selected Piper voice files are missing";
        }
        return false;
    }

    EnsureDirectory(output_wav.parent_path());
    std::filesystem::path log_path = output_wav;
    log_path.replace_extension(".log");
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
        " --output_file " + QuoteShellPath(output_wav) +
        " > " + QuoteShellPath(log_path) +
        " 2>&1";
#else
    const std::string command =
        QuoteShellPath(executable) +
        " --model " + QuoteShellPath(model) +
        " --config " + QuoteShellPath(config) +
        " --output_file " + QuoteShellPath(output_wav) +
        " < " + QuoteShellPath(input_path) +
        " > " + QuoteShellPath(log_path) +
        " 2>&1";
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
    std::error_code exists_error;
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
