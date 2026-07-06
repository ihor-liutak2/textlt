#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "json_utils.hpp"

namespace textlt {

struct PiperRunOptions {
    bool use_cuda = false;
    double noise_scale = 0.667;
    double sentence_silence_seconds = 0.15;
    int speaker_id = 0;
};

class PiperManager {
public:
    static std::filesystem::path UserDataDirectory();
    static std::filesystem::path RuntimeDirectory();
    static std::filesystem::path ModelsDirectory();
    static std::filesystem::path RuntimeDownloadArchivePath();
    static std::string RuntimeDownloadUrl();
    static std::filesystem::path RuntimeExecutablePath();
    static bool RuntimeInstalled();
    static std::filesystem::path ServerExecutablePath();
    static bool ServerInstalled();
    static std::filesystem::path AssetPathFromUrl(const std::string& url);
    static std::filesystem::path VoiceDirectory(const Json& voice);
    static std::filesystem::path VoiceModelPath(const Json& voice);
    static std::filesystem::path VoiceConfigPath(const Json& voice);
    static std::vector<std::filesystem::path> VoiceModelPathCandidates(const Json& voice);
    static std::vector<std::filesystem::path> VoiceConfigPathCandidates(const Json& voice);
    static bool VoiceInstalled(const Json& voice);
    static bool FindVoiceById(const std::string& voice_id, Json* voice);
    static bool RunToFile(const Json& voice,
                          const std::string& text,
                          const std::filesystem::path& output_wav,
                          std::string* error);
    static bool RunToFile(const Json& voice,
                          const std::string& text,
                          const std::filesystem::path& output_wav,
                          const PiperRunOptions& options,
                          std::string* error);

    static std::string QuoteShellPath(const std::filesystem::path& path);
    static std::string QuotePowerShellSingle(const std::filesystem::path& path);
};

} // namespace textlt
