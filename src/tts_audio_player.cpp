#include "tts_audio_player.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <system_error>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace textlt {
namespace {

std::vector<std::filesystem::path> PathDirectories() {
    std::vector<std::filesystem::path> directories;
    const char* path_env = std::getenv("PATH");
    if (!path_env || std::string(path_env).empty()) {
        return directories;
    }

#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif

    std::string path(path_env);
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = path.find(separator, start);
        const std::string item = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!item.empty()) {
            directories.emplace_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return directories;
}

bool ExecutableExists(const std::string& executable) {
    if (executable.empty()) {
        return false;
    }

    std::error_code error;
    const std::filesystem::path direct(executable);
    if (direct.has_parent_path() && std::filesystem::exists(direct, error)) {
        return true;
    }

    for (const std::filesystem::path& directory : PathDirectories()) {
        std::filesystem::path candidate = directory / executable;
        error.clear();
        if (std::filesystem::exists(candidate, error)) {
            return true;
        }
#ifdef _WIN32
        candidate = directory / (executable + ".exe");
        error.clear();
        if (std::filesystem::exists(candidate, error)) {
            return true;
        }
#endif
    }
    return false;
}

std::string NormalizedSelectedPlayerId(std::string id) {
    if (id.empty()) {
        return "auto";
    }
    return id;
}

bool IsCustomCommand(const TtsAudioPlayer::PlayerCommand& command) {
    return command.id == "custom";
}

TtsAudioPlayer::PlayerCommand CustomPlayerCommand(const TtsAudioPlayer::PlayerSettings& settings) {
    return {
        "custom",
        "Custom command",
        "",
        {},
        {},
        settings.custom_command,
        false,
    };
}

bool PlayerAvailable(const TtsAudioPlayer::PlayerCommand& command) {
    if (IsCustomCommand(command)) {
        return !command.custom_command.empty();
    }
    return ExecutableExists(command.executable);
}

std::vector<std::string> BuildArguments(
    const TtsAudioPlayer::PlayerCommand& command,
    const std::filesystem::path& audio_file) {
    std::vector<std::string> arguments;
    arguments.reserve(command.arguments_before_file.size() + command.arguments_after_file.size() + 1);
    arguments.insert(arguments.end(), command.arguments_before_file.begin(), command.arguments_before_file.end());
    arguments.push_back(audio_file.string());
    arguments.insert(arguments.end(), command.arguments_after_file.begin(), command.arguments_after_file.end());
    return arguments;
}

void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string BuildCustomCommandLine(const TtsAudioPlayer::PlayerCommand& command,
                                   const std::filesystem::path& audio_file) {
    std::string command_line = command.custom_command;
    const std::string quoted_file = TtsAudioPlayer::QuoteShellArgument(audio_file.string());
    if (command_line.find("{file}") != std::string::npos) {
        ReplaceAll(command_line, "{file}", quoted_file);
    } else {
        if (!command_line.empty()) {
            command_line += ' ';
        }
        command_line += quoted_file;
    }
    return command_line;
}

std::vector<TtsAudioPlayer::PlayerCommand> AvailableNonCustomPlayers() {
    std::vector<TtsAudioPlayer::PlayerCommand> players;
    for (const TtsAudioPlayer::PlayerCommand& command : TtsAudioPlayer::CandidatePlayers()) {
        if (PlayerAvailable(command)) {
            players.push_back(command);
        }
    }
    return players;
}

std::vector<TtsAudioPlayer::PlayerCommand> AvailablePlayersForSettings(
    const TtsAudioPlayer::PlayerSettings& settings) {
    std::vector<TtsAudioPlayer::PlayerCommand> players = AvailableNonCustomPlayers();
    const TtsAudioPlayer::PlayerCommand custom = CustomPlayerCommand(settings);
    if (PlayerAvailable(custom)) {
        players.push_back(custom);
    }
    return players;
}

TtsAudioPlayer::PlayerCommand ResolvePlayer(const TtsAudioPlayer::PlayerSettings& settings) {
    const std::string selected_id = NormalizedSelectedPlayerId(settings.selected_player_id);
    const std::vector<TtsAudioPlayer::PlayerCommand> players = AvailablePlayersForSettings(settings);

    if (selected_id != "auto") {
        const auto iter = std::find_if(
            players.begin(),
            players.end(),
            [&](const TtsAudioPlayer::PlayerCommand& command) {
                return command.id == selected_id;
            });
        if (iter != players.end()) {
            return *iter;
        }
    }

    return players.empty() ? TtsAudioPlayer::PlayerCommand{} : players.front();
}

std::string BuildStatusText(const TtsAudioPlayer::PlayerCommand& command,
                            bool available,
                            bool current) {
    std::vector<std::string> parts;
    parts.push_back(available ? "available" : "not found");
    if (current) {
        parts.push_back("current");
    }
    if (command.recommended) {
        parts.push_back("recommended");
    }
    if (IsCustomCommand(command) && command.custom_command.empty()) {
        parts.push_back("not configured");
    }

    std::string text;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            text += ", ";
        }
        text += parts[index];
    }
    return text;
}

#ifndef _WIN32
bool PlayWithPosixProcess(
    const TtsAudioPlayer::PlayerCommand& command,
    const std::filesystem::path& audio_file,
    std::atomic<bool>* stop_requested,
    std::string* error) {
    std::string shell_command;
    std::vector<std::string> arguments;
    std::string executable = command.executable;

    if (IsCustomCommand(command)) {
        shell_command = BuildCustomCommandLine(command, audio_file);
        executable = "/bin/sh";
        arguments = {"-c", shell_command};
    } else {
        arguments = BuildArguments(command, audio_file);
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const std::string& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        if (error) {
            *error = "Cannot start audio player";
        }
        return false;
    }

    if (pid == 0) {
        execvp(executable.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    while (true) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            break;
        }
        if (waited < 0) {
            if (error) {
                *error = "Audio player wait failed";
            }
            return false;
        }
        if (stop_requested && stop_requested->load()) {
            kill(pid, SIGTERM);
            for (int attempt = 0; attempt < 20; ++attempt) {
                const pid_t stopped = waitpid(pid, &status, WNOHANG);
                if (stopped == pid) {
                    if (error) {
                        *error = "Playback stopped";
                    }
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            if (error) {
                *error = "Playback stopped";
            }
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }
    if (error) {
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
            *error = "Audio player command was not found";
        } else {
            *error = "Audio player failed";
        }
    }
    return false;
}
#endif

#ifdef _WIN32
std::string BuildWindowsCommand(
    const TtsAudioPlayer::PlayerCommand& command,
    const std::filesystem::path& audio_file) {
    if (IsCustomCommand(command)) {
        return BuildCustomCommandLine(command, audio_file);
    }

    std::ostringstream command_line;
    command_line << TtsAudioPlayer::QuoteShellArgument(command.executable);
    for (const std::string& argument : BuildArguments(command, audio_file)) {
        command_line << ' ' << TtsAudioPlayer::QuoteShellArgument(argument);
    }
    return command_line.str();
}
#endif

} // namespace

std::vector<TtsAudioPlayer::PlayerCommand> TtsAudioPlayer::CandidatePlayers() {
#ifdef _WIN32
    return {
        {"powershell", "PowerShell SoundPlayer", "powershell.exe",
         {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
          "$p=$args[0];$player=New-Object System.Media.SoundPlayer $p;$player.PlaySync()", "--"},
         {}, {}, true},
        {"mpv", "mpv", "mpv", {"--really-quiet", "--no-terminal"}, {}, {}, false},
        {"ffplay", "ffplay", "ffplay", {"-nodisp", "-autoexit", "-loglevel", "error"}, {}, {}, false},
    };
#elif defined(__APPLE__)
    return {
        {"afplay", "afplay", "afplay", {}, {}, {}, true},
        {"mpv", "mpv", "mpv", {"--really-quiet", "--no-terminal"}, {}, {}, false},
        {"ffplay", "ffplay", "ffplay", {"-nodisp", "-autoexit", "-loglevel", "error"}, {}, {}, false},
        {"open", "open default app", "open", {}, {}, {}, false},
    };
#else
    return {
        {"mpv", "mpv", "mpv", {"--really-quiet", "--no-terminal"}, {}, {}, true},
        {"ffplay", "ffplay", "ffplay", {"-nodisp", "-autoexit", "-loglevel", "error"}, {}, {}, false},
        {"pw-play", "PipeWire pw-play", "pw-play", {}, {}, {}, false},
        {"paplay", "PulseAudio paplay", "paplay", {}, {}, {}, false},
        {"aplay", "ALSA aplay", "aplay", {}, {}, {}, false},
        {"xdg-open", "xdg-open default app", "xdg-open", {}, {}, {}, false},
    };
#endif
}

std::vector<TtsAudioPlayer::PlayerStatus> TtsAudioPlayer::PlayerStatuses(
    const PlayerSettings& settings) {
    std::vector<PlayerStatus> statuses;
    const PlayerCommand current = ResolvePlayer(settings);

    for (const PlayerCommand& command : CandidatePlayers()) {
        const bool available = PlayerAvailable(command);
        const bool is_current = !current.id.empty() && current.id == command.id;
        statuses.push_back({command, available, is_current, BuildStatusText(command, available, is_current)});
    }

    const PlayerCommand custom = CustomPlayerCommand(settings);
    const bool custom_available = PlayerAvailable(custom);
    const bool custom_current = !current.id.empty() && current.id == custom.id;
    statuses.push_back({custom, custom_available, custom_current, BuildStatusText(custom, custom_available, custom_current)});
    return statuses;
}

std::vector<TtsAudioPlayer::PlayerCommand> TtsAudioPlayer::AvailablePlayers(
    const PlayerSettings& settings) {
    return AvailablePlayersForSettings(settings);
}

bool TtsAudioPlayer::HasAvailablePlayer(const PlayerSettings& settings) {
    return !AvailablePlayers(settings).empty();
}

std::string TtsAudioPlayer::SelectedPlayerLabel(const PlayerSettings& settings) {
    const PlayerCommand player = ResolvePlayer(settings);
    return player.label;
}

std::string TtsAudioPlayer::SelectedPlayerStatusText(const PlayerSettings& settings) {
    const PlayerCommand player = ResolvePlayer(settings);
    if (player.id.empty()) {
        return DependencyHelpText();
    }
    return BuildStatusText(player, true, true);
}

std::string TtsAudioPlayer::DependencyHelpText() {
#ifdef _WIN32
    return "No audio player found. Enable PowerShell audio playback or install mpv/ffmpeg.";
#elif defined(__APPLE__)
    return "No audio player found. macOS afplay should be available; otherwise install mpv or ffmpeg.";
#else
    return "No audio player found. Recommended: sudo apt install mpv. Fallbacks: ffmpeg, pipewire-bin, pulseaudio-utils, or alsa-utils.";
#endif
}

std::string TtsAudioPlayer::QuoteShellArgument(const std::string& value) {
#ifdef _WIN32
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

bool TtsAudioPlayer::PlayFileBlocking(
    const std::filesystem::path& audio_file,
    std::atomic<bool>* stop_requested,
    std::string* error,
    const PlayerSettings& settings) const {
    std::error_code exists_error;
    if (!std::filesystem::exists(audio_file, exists_error)) {
        if (error) {
            *error = "Audio file does not exist";
        }
        return false;
    }

    const PlayerCommand player = ResolvePlayer(settings);
    if (player.id.empty()) {
        if (error) {
            *error = DependencyHelpText();
        }
        return false;
    }

#ifdef _WIN32
    if (stop_requested && stop_requested->load()) {
        if (error) {
            *error = "Playback stopped";
        }
        return false;
    }
    const int result = std::system(BuildWindowsCommand(player, audio_file).c_str());
    if (result == 0) {
        return true;
    }
    if (error) {
        *error = "Audio player failed: " + player.label;
    }
    return false;
#else
    std::string last_error;
    if (PlayWithPosixProcess(player, audio_file, stop_requested, &last_error)) {
        return true;
    }
    if (error) {
        *error = last_error.empty() ? "Audio player failed: " + player.label : last_error;
    }
    return false;
#endif
}

} // namespace textlt
