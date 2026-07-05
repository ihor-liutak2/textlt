#include "tts_audio_player.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestCandidatePlayersAreDefined() {
    const auto players = textlt::TtsAudioPlayer::CandidatePlayers();
    Expect(!players.empty(), "candidate player list must not be empty");
    for (const auto& player : players) {
        Expect(!player.id.empty(), "player id must not be empty");
        Expect(!player.label.empty(), "player label must not be empty");
        Expect(!player.executable.empty(), "player executable must not be empty");
    }
}

void TestShellQuoting() {
    const std::string quoted = textlt::TtsAudioPlayer::QuoteShellArgument("a b'c");
    Expect(!quoted.empty(), "quoted shell argument must not be empty");
#ifndef _WIN32
    Expect(quoted.front() == '\'', "POSIX quoted argument must start with single quote");
    Expect(quoted.back() == '\'', "POSIX quoted argument must end with single quote");
    Expect(quoted.find("'\\''") != std::string::npos, "POSIX quoted argument must escape single quote");
#endif
}

void TestDependencyHelp() {
    Expect(!textlt::TtsAudioPlayer::DependencyHelpText().empty(), "dependency help must not be empty");
}

void TestPlayerStatusesIncludeCustom() {
    textlt::TtsAudioPlayer::PlayerSettings settings;
    settings.selected_player_id = "custom";
    settings.custom_command = "echo {file}";

    const auto statuses = textlt::TtsAudioPlayer::PlayerStatuses(settings);
    bool found_custom = false;
    bool custom_current = false;
    for (const auto& status : statuses) {
        if (status.command.id == "custom") {
            found_custom = true;
            custom_current = status.current;
            Expect(status.available, "configured custom player must be available");
        }
    }
    Expect(found_custom, "player status list must include custom command row");
    Expect(custom_current, "selected custom player must be current");
}

void TestAutoSettingsAreValid() {
    const textlt::TtsAudioPlayer::PlayerSettings settings;
    const auto statuses = textlt::TtsAudioPlayer::PlayerStatuses(settings);
    Expect(!statuses.empty(), "player status list must not be empty");
    Expect(!textlt::TtsAudioPlayer::SelectedPlayerStatusText(settings).empty(),
           "selected player status text must not be empty");
}

} // namespace

int main() {
    TestCandidatePlayersAreDefined();
    TestShellQuoting();
    TestDependencyHelp();
    TestPlayerStatusesIncludeCustom();
    TestAutoSettingsAreValid();
    std::cout << "tts audio player tests passed\n";
    return 0;
}
