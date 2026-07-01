#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

class TtsAudioPlayer {
public:
    struct PlayerCommand {
        std::string id;
        std::string label;
        std::string executable;
        std::vector<std::string> arguments_before_file;
        std::vector<std::string> arguments_after_file;
    };

    static std::vector<PlayerCommand> CandidatePlayers();
    static std::vector<PlayerCommand> AvailablePlayers();
    static bool HasAvailablePlayer();
    static std::string SelectedPlayerLabel();
    static std::string DependencyHelpText();
    static std::string QuoteShellArgument(const std::string& value);

    bool PlayFileBlocking(
        const std::filesystem::path& audio_file,
        std::atomic<bool>* stop_requested,
        std::string* error) const;
};

} // namespace textlt
