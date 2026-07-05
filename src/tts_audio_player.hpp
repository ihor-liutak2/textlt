#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace textlt {

class TtsAudioPlayer {
public:
    struct PlayerSettings {
        PlayerSettings(std::string selected_player = "auto", std::string custom = {})
            : selected_player_id(std::move(selected_player)),
              custom_command(std::move(custom)) {}

        std::string selected_player_id;
        std::string custom_command;
    };

    struct PlayerCommand {
        std::string id;
        std::string label;
        std::string executable;
        std::vector<std::string> arguments_before_file;
        std::vector<std::string> arguments_after_file;
        std::string custom_command;
        bool recommended = false;
    };

    struct PlayerStatus {
        PlayerCommand command;
        bool available = false;
        bool current = false;
        std::string status_text;
    };

    static std::vector<PlayerCommand> CandidatePlayers();
    static std::vector<PlayerStatus> PlayerStatuses(const PlayerSettings& settings = PlayerSettings());
    static std::vector<PlayerCommand> AvailablePlayers(const PlayerSettings& settings = PlayerSettings());
    static bool HasAvailablePlayer(const PlayerSettings& settings = PlayerSettings());
    static std::string SelectedPlayerLabel(const PlayerSettings& settings = PlayerSettings());
    static std::string SelectedPlayerStatusText(const PlayerSettings& settings = PlayerSettings());
    static std::string DependencyHelpText();
    static std::string QuoteShellArgument(const std::string& value);

    bool PlayFileBlocking(
        const std::filesystem::path& audio_file,
        std::atomic<bool>* stop_requested,
        std::string* error,
        const PlayerSettings& settings = PlayerSettings()) const;
};

} // namespace textlt
