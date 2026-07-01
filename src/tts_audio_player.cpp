#include "tts_audio_player.hpp"

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

#ifndef _WIN32
bool PlayWithPosixProcess(
    const TtsAudioPlayer::PlayerCommand& command,
    const std::filesystem::path& audio_file,
    std::atomic<bool>* stop_requested,
    std::string* error) {
    const std::vector<std::string> arguments = BuildArguments(command, audio_file);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(command.executable.c_str()));
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
        execvp(command.executable.c_str(), argv.data());
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
        *error = "Audio player failed";
    }
    return false;
}
#endif

#ifdef _WIN32
std::string BuildWindowsCommand(
    const TtsAudioPlayer::PlayerCommand& command,
    const std::filesystem::path& audio_file) {
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
         {}},
    };
#elif defined(__APPLE__)
    return {
        {"afplay", "afplay", "afplay", {}, {}},
        {"ffplay", "ffplay", "ffplay", {"-nodisp", "-autoexit", "-loglevel", "error"}, {}},
    };
#else
    return {
        {"pw-play", "PipeWire pw-play", "pw-play", {}, {}},
        {"paplay", "PulseAudio paplay", "paplay", {}, {}},
        {"aplay", "ALSA aplay", "aplay", {}, {}},
        {"ffplay", "ffplay", "ffplay", {"-nodisp", "-autoexit", "-loglevel", "error"}, {}},
    };
#endif
}

std::vector<TtsAudioPlayer::PlayerCommand> TtsAudioPlayer::AvailablePlayers() {
    std::vector<PlayerCommand> players;
    for (const PlayerCommand& command : CandidatePlayers()) {
        if (ExecutableExists(command.executable)) {
            players.push_back(command);
        }
    }
    return players;
}

bool TtsAudioPlayer::HasAvailablePlayer() {
    return !AvailablePlayers().empty();
}

std::string TtsAudioPlayer::SelectedPlayerLabel() {
    const std::vector<PlayerCommand> players = AvailablePlayers();
    return players.empty() ? std::string() : players.front().label;
}

std::string TtsAudioPlayer::DependencyHelpText() {
#ifdef _WIN32
    return "No audio player found. Enable PowerShell and Windows audio playback.";
#elif defined(__APPLE__)
    return "No audio player found. Install ffmpeg or use macOS afplay.";
#else
    return "No audio player found. Install pipewire-bin, pulseaudio-utils, alsa-utils, or ffmpeg.";
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
    std::string* error) const {
    std::error_code exists_error;
    if (!std::filesystem::exists(audio_file, exists_error)) {
        if (error) {
            *error = "Audio file does not exist";
        }
        return false;
    }

    const std::vector<PlayerCommand> players = AvailablePlayers();
    if (players.empty()) {
        if (error) {
            *error = DependencyHelpText();
        }
        return false;
    }

    const PlayerCommand& player = players.front();
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
        *error = "Audio player failed";
    }
    return false;
#else
    return PlayWithPosixProcess(player, audio_file, stop_requested, error);
#endif
}

} // namespace textlt
