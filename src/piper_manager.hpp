#pragma once

#include <filesystem>
#include <string>

#include "json_utils.hpp"

namespace textlt {

class PiperManager {
public:
    static std::filesystem::path UserDataDirectory();
    static std::filesystem::path RuntimeDirectory();
    static std::filesystem::path ModelsDirectory();
    static std::filesystem::path RuntimeDownloadArchivePath();
    static std::string RuntimeDownloadUrl();
    static std::filesystem::path RuntimeExecutablePath();
    static bool RuntimeInstalled();
    static bool VoiceInstalled(const Json& voice);
    static bool FindVoiceById(const std::string& voice_id, Json* voice);
    static bool RunToFile(const Json& voice,
                          const std::string& text,
                          const std::filesystem::path& output_wav,
                          std::string* error);

    static std::string QuoteShellPath(const std::filesystem::path& path);
    static std::string QuotePowerShellSingle(const std::filesystem::path& path);
};

} // namespace textlt
