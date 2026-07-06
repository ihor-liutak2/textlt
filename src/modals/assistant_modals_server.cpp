#include "assistant_modals.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "piper_manager.hpp"
#include "remote/remote_http_client.hpp"

namespace textlt {
namespace {

constexpr const char* kPiperServerHost = "127.0.0.1";
constexpr int kDefaultPiperServerPort = 59123;
constexpr double kDefaultNoiseScale = 0.667;
constexpr double kMinNoiseScale = 0.45;
constexpr double kMaxNoiseScale = 0.85;
constexpr double kDefaultSentenceSilence = 0.15;
constexpr double kMinSentenceSilence = 0.0;
constexpr double kMaxSentenceSilence = 0.50;
constexpr int kMinSpeakerId = 0;
constexpr int kMaxSpeakerId = 999;

ftxui::Element StatusLine(const std::string& label,
                          const std::string& value,
                          const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(label) | bold | color(theme.modal_accent) | size(WIDTH, EQUAL, 13),
        text(value.empty() ? "-" : value) | color(theme.modal_text_color) | flex,
    });
}

ftxui::Element ServerInputRow(const std::string& label,
                              const ftxui::Component& input,
                              const std::string& hint,
                              const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(label) | bold | color(theme.modal_accent) | size(WIDTH, EQUAL, 18),
        input->Render() | color(theme.modal_input_fg) | size(WIDTH, EQUAL, 10) | border,
        text(" " + hint) | color(theme.modal_text_color) | dim | flex,
    });
}

ftxui::Element SectionTitle(const std::string& title, const Theme& theme) {
    using namespace ftxui;
    return text(title) | bold | color(theme.modal_text_color);
}

ftxui::Element Panel(const std::string& title,
                     ftxui::Elements rows,
                     const Theme& theme) {
    using namespace ftxui;
    rows.insert(rows.begin(), separator() | color(theme.modal_border));
    rows.insert(rows.begin(), SectionTitle(title, theme));
    return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, 42);
}

ftxui::Element ButtonSlot(const ftxui::Component& button) {
    using namespace ftxui;
    return button->Render() | size(WIDTH, EQUAL, 17);
}

std::string SelectedPiperLanguage(const std::vector<std::string>& labels,
                                  int selected_language) {
    return selected_language >= 0 &&
            selected_language < static_cast<int>(labels.size())
        ? labels[selected_language]
        : "";
}

std::string SelectedPiperVoiceLabel(const std::vector<std::string>& labels,
                                    int selected_voice) {
    return selected_voice >= 0 && selected_voice < static_cast<int>(labels.size())
        ? labels[selected_voice]
        : "-";
}

bool FindSelectedPiperVoiceForServer(const std::string& selected_language,
                                     int selected_voice,
                                     Json* selected) {
    using namespace assistant_modal_detail;

    Json root;
    if (LoadUserRegistryJson(RegistryKind::Piper, &root) != RegistryLoadResult::Loaded) {
        return false;
    }

    const auto voices = root.find("voices");
    if (voices == root.end() || !voices->is_array()) {
        return false;
    }

    int visible_index = 0;
    for (const Json& voice : *voices) {
        if (!voice.is_object()) {
            continue;
        }
        const std::string language =
            JsonString(voice, "language_name", JsonString(voice, "language_code"));
        const std::string country = JsonString(voice, "country");
        const std::string key = country.empty() ? language : language + " - " + country;
        if (key != selected_language) {
            continue;
        }
        if (visible_index == selected_voice) {
            *selected = voice;
            return true;
        }
        ++visible_index;
    }
    return false;
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

bool ParseIntRange(const std::string& value,
                   int fallback,
                   int minimum,
                   int maximum,
                   int* parsed) {
    try {
        const int result = value.empty() ? fallback : std::stoi(value);
        if (result < minimum || result > maximum) {
            return false;
        }
        *parsed = result;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseDoubleRange(const std::string& value,
                      double fallback,
                      double minimum,
                      double maximum,
                      double* parsed) {
    try {
        const double result = value.empty() ? fallback : std::stod(value);
        if (result < minimum || result > maximum) {
            return false;
        }
        *parsed = result;
        return true;
    } catch (...) {
        return false;
    }
}

std::string BaseUrl(int port) {
    return std::string("http://") + kPiperServerHost + ":" + std::to_string(port);
}


std::string FormatDuration(long long seconds) {
    if (seconds < 0) {
        return "-";
    }
    const long long hours = seconds / 3600;
    const long long minutes = (seconds % 3600) / 60;
    const long long rest = seconds % 60;
    std::ostringstream out;
    if (hours > 0) {
        out << hours << "h " << minutes << "m " << rest << "s";
    } else if (minutes > 0) {
        out << minutes << "m " << rest << "s";
    } else {
        out << rest << "s";
    }
    return out.str();
}

std::string JsonStringValue(const Json& object,
                            const char* key,
                            const std::string& fallback = "-") {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? "true" : "false";
    }
    if (it->is_number_unsigned()) {
        return std::to_string(it->get<unsigned long long>());
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<long long>());
    }
    if (it->is_number_float()) {
        return std::to_string(it->get<double>());
    }
    return fallback;
}

long long JsonLongValue(const Json& object, const char* key, long long fallback = -1) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (it->is_number_unsigned()) {
        return static_cast<long long>(it->get<unsigned long long>());
    }
    if (it->is_number_integer()) {
        return it->get<long long>();
    }
    return fallback;
}

std::filesystem::path PiperLogDirectory() {
    const std::filesystem::path data = PiperManager::UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "piper" / "logs";
}

std::string BuildServerCommand(const std::filesystem::path& server,
                               const std::filesystem::path& piper,
                               int port,
                               const std::filesystem::path& model,
                               const std::filesystem::path& config,
                               bool use_cuda) {
    const std::filesystem::path log_dir = PiperLogDirectory();
#ifdef _WIN32
    std::string command = "start \"\" /B " + PiperManager::QuoteShellPath(server) +
        " --host " + kPiperServerHost +
        " --port " + std::to_string(port) +
        " --piper " + PiperManager::QuoteShellPath(piper) +
        " --model " + PiperManager::QuoteShellPath(model) +
        " --config " + PiperManager::QuoteShellPath(config) +
        " --models-dir " + PiperManager::QuoteShellPath(PiperManager::ModelsDirectory()) +
        " --log-dir " + PiperManager::QuoteShellPath(log_dir);
    if (use_cuda) {
        command += " --cuda";
    }
    command += " > NUL 2>&1";
    return command;
#else
    std::string command = PiperManager::QuoteShellPath(server) +
        " --host " + kPiperServerHost +
        " --port " + std::to_string(port) +
        " --piper " + PiperManager::QuoteShellPath(piper) +
        " --model " + PiperManager::QuoteShellPath(model) +
        " --config " + PiperManager::QuoteShellPath(config) +
        " --models-dir " + PiperManager::QuoteShellPath(PiperManager::ModelsDirectory()) +
        " --log-dir " + PiperManager::QuoteShellPath(log_dir);
    if (use_cuda) {
        command += " --cuda";
    }
    command += " >/dev/null 2>&1 &";
    return command;
#endif
}

bool JsonBoolValue(const Json& object, const char* key, bool fallback = false) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::string VoiceStateLabel(const std::string& status) {
    if (status == "ready") {
        return "Ready";
    }
    if (status == "loading") {
        return "Loading";
    }
    if (status == "failed") {
        return "Failed";
    }
    return "Not loaded";
}


} // namespace

bool AssistantSettingsModalContent::ReadPiperServerSettings(int* port,
                                                            PiperRunOptions* options,
                                                            std::string* error) const {
    int resolved_port = kDefaultPiperServerPort;
    if (!ParseIntRange(piper_server_port_, kDefaultPiperServerPort, 1024, 65535, &resolved_port)) {
        if (error) {
            *error = "Port must be empty or 1024..65535";
        }
        return false;
    }

    PiperRunOptions resolved_options;
    resolved_options.use_cuda = piper_server_use_cuda_;
    if (!ParseDoubleRange(
            piper_server_noise_scale_,
            kDefaultNoiseScale,
            kMinNoiseScale,
            kMaxNoiseScale,
            &resolved_options.noise_scale)) {
        if (error) {
            *error = "Noise scale must be 0.45..0.85";
        }
        return false;
    }
    if (!ParseDoubleRange(
            piper_server_sentence_silence_,
            kDefaultSentenceSilence,
            kMinSentenceSilence,
            kMaxSentenceSilence,
            &resolved_options.sentence_silence_seconds)) {
        if (error) {
            *error = "Sentence silence must be 0.00..0.50 seconds";
        }
        return false;
    }
    if (!ParseIntRange(
            piper_server_speaker_id_,
            0,
            kMinSpeakerId,
            kMaxSpeakerId,
            &resolved_options.speaker_id)) {
        if (error) {
            *error = "Speaker ID must be 0..999";
        }
        return false;
    }

    if (port) {
        *port = resolved_port;
    }
    if (options) {
        *options = resolved_options;
    }
    return true;
}

void AssistantSettingsModalContent::ApplyPiperServerStatusResponse(
    const RemoteHttpResponse& response,
    int port,
    const std::string& offline_status) {
    if (!response.ok) {
        piper_server_running_ = "Stopped";
        piper_server_voice_state_ = "Not loaded";
        piper_server_active_voice_ = "-";
        piper_server_uptime_ = "-";
        piper_server_pid_ = "-";
        piper_server_requests_ = "-";
        piper_server_status_ = response.error.empty()
            ? offline_status
            : offline_status + ": " + response.error;
        return;
    }

    const Json body = Json::parse(response.body, nullptr, false);
    if (!body.is_object()) {
        piper_server_running_ = "Running";
        piper_server_voice_state_ = "Not loaded";
        piper_server_active_voice_ = "-";
        piper_server_uptime_ = "-";
        piper_server_pid_ = "-";
        piper_server_requests_ = "-";
        piper_server_status_ = "Server is running on 127.0.0.1:" + std::to_string(port);
        return;
    }

    piper_server_running_ = "Running";
    piper_server_voice_state_ = VoiceStateLabel(JsonStringValue(body, "voice_status", "not_loaded"));
    piper_server_active_voice_ = JsonStringValue(body, "active_voice", "-");
    piper_server_uptime_ = FormatDuration(JsonLongValue(body, "uptime_seconds"));
    piper_server_pid_ = JsonStringValue(body, "pid");
    piper_server_requests_ = JsonStringValue(body, "requests");
    piper_server_status_ = "Server is running on 127.0.0.1:" + std::to_string(port);
    const std::string backend_message = JsonStringValue(body, "backend_message", "");
    if (piper_server_voice_state_ == "Failed" && !backend_message.empty() && backend_message != "-") {
        piper_server_status_ += ". " + backend_message;
    }
}

void AssistantSettingsModalContent::RefreshPiperServerStatus() {
    int port = 0;
    PiperRunOptions options;
    std::string error;
    if (!ReadPiperServerSettings(&port, &options, &error)) {
        piper_server_status_ = error;
        return;
    }
    if (!PiperManager::ServerInstalled()) {
        piper_server_status_ = "Server executable missing";
        return;
    }
    const RemoteHttpClient client;
    const RemoteHttpResponse response = client.Request(
        "GET",
        BaseUrl(port) + "/status",
        {},
        {},
        3);
    ApplyPiperServerStatusResponse(response, port, "Server is not running");
}

void AssistantSettingsModalContent::StartPiperServer() {
    int port = 0;
    PiperRunOptions options;
    std::string error;
    if (!ReadPiperServerSettings(&port, &options, &error)) {
        piper_server_status_ = error;
        return;
    }

    const RemoteHttpClient client;
    const RemoteHttpResponse existing = client.Request("GET", BaseUrl(port) + "/status", {}, {}, 2);
    if (existing.ok) {
        ApplyPiperServerStatusResponse(existing, port, "Server is not running");
        piper_server_status_ = "Server is already running on 127.0.0.1:" + std::to_string(port);
        return;
    }

    const std::filesystem::path server = PiperManager::ServerExecutablePath();
    if (server.empty()) {
        piper_server_status_ = "textlt-piper-server is not installed";
        return;
    }

    const std::filesystem::path piper = PiperManager::RuntimeExecutablePath();
    if (piper.empty()) {
        piper_server_status_ = "Piper runtime is not installed";
        return;
    }

    Json voice;
    const std::string language = SelectedPiperLanguage(tts_language_labels_, selected_tts_language_);
    if (!FindSelectedPiperVoiceForServer(language, selected_tts_voice_, &voice)) {
        piper_server_status_ = "No Piper voice selected";
        return;
    }

    const std::filesystem::path model = FirstExistingPath(PiperManager::VoiceModelPathCandidates(voice));
    const std::filesystem::path config = FirstExistingPath(PiperManager::VoiceConfigPathCandidates(voice));
    if (model.empty() || config.empty()) {
        piper_server_status_ = "Selected Piper voice files are missing";
        return;
    }

    const std::string command = BuildServerCommand(
        server,
        piper,
        port,
        model,
        config,
        options.use_cuda);
    const int result = std::system(command.c_str());
    if (result != 0) {
        piper_server_running_ = "Stopped";
        piper_server_voice_state_ = "Failed";
        piper_server_status_ = "Server start command failed";
        return;
    }

    piper_server_status_ = "Starting server and loading voice...";
    for (int attempt = 0; attempt < 80; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const RemoteHttpResponse response = client.Request("GET", BaseUrl(port) + "/status", {}, {}, 2);
        if (!response.ok) {
            continue;
        }
        ApplyPiperServerStatusResponse(response, port, "Server is not running");
        const Json body = Json::parse(response.body, nullptr, false);
        if (body.is_object() && JsonBoolValue(body, "voice_ready")) {
            piper_server_status_ = "Server started and voice is ready";
            return;
        }
    }
    RefreshPiperServerStatus();
}

void AssistantSettingsModalContent::StopPiperServer() {
    int port = 0;
    PiperRunOptions options;
    std::string error;
    if (!ReadPiperServerSettings(&port, &options, &error)) {
        piper_server_status_ = error;
        return;
    }

    const RemoteHttpClient client;
    const RemoteHttpResponse response = client.Request(
        "POST",
        BaseUrl(port) + "/shutdown",
        {"Content-Type: application/json"},
        "{}",
        3);
    if (response.ok) {
        piper_server_running_ = "Stopped";
        piper_server_voice_state_ = "Not loaded";
        piper_server_active_voice_ = "-";
        piper_server_uptime_ = "-";
        piper_server_pid_ = "-";
        piper_server_requests_ = "-";
        piper_server_status_ = "Server stop requested";
    } else {
        piper_server_status_ = response.error.empty()
            ? "Server stop failed"
            : "Server stop failed: " + response.error;
    }
}

void AssistantSettingsModalContent::ReloadPiperServer() {
    StopPiperServer();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    StartPiperServer();
}

void AssistantSettingsModalContent::CheckPiperServerHealth() {
    int port = 0;
    PiperRunOptions options;
    std::string error;
    if (!ReadPiperServerSettings(&port, &options, &error)) {
        piper_server_status_ = error;
        return;
    }

    const RemoteHttpClient client;
    const RemoteHttpResponse response = client.Request(
        "GET",
        BaseUrl(port) + "/health",
        {},
        {},
        3);
    ApplyPiperServerStatusResponse(response, port, "Server is not running");
}

bool AssistantSettingsModalContent::GeneratePiperTestAudio(const Json& voice,
                                                           const std::string& text,
                                                           const std::filesystem::path& output_wav,
                                                           std::string* error) {
    int port = 0;
    PiperRunOptions options;
    std::string settings_error;
    if (!ReadPiperServerSettings(&port, &options, &settings_error)) {
        if (error) {
            *error = settings_error;
        }
        return false;
    }

    const bool ok = PiperManager::RunToFile(voice, text, output_wav, options, error);
    RefreshPiperServerStatus();
    return ok;
}

ftxui::Element AssistantSettingsModalContent::RenderPiperServerTab(const Theme& theme) {
    using namespace ftxui;

    int port = 0;
    PiperRunOptions options;
    std::string settings_error;
    const bool settings_ok = ReadPiperServerSettings(&port, &options, &settings_error);

    Element validation = text(settings_ok ? "Settings OK" : settings_error) |
        color(settings_ok ? theme.modal_text_color : theme.button_danger);
    if (!settings_ok) {
        validation = validation | bold;
    }

    const std::string address = std::string(kPiperServerHost) + ":" +
        std::to_string(settings_ok ? port : kDefaultPiperServerPort);

    Element button_rows = vbox({
        hbox({
            ButtonSlot(piper_server_refresh_button_),
            text(" "),
            ButtonSlot(piper_server_start_button_),
            text(" "),
            ButtonSlot(piper_server_reload_button_),
        }),
        hbox({
            ButtonSlot(piper_server_health_button_),
            text(" "),
            ButtonSlot(piper_server_shutdown_button_),
        }),
    });

    Element status_panel = Panel("Server state", {
        StatusLine("Server", piper_server_running_, theme),
        StatusLine("Voice state", piper_server_voice_state_, theme),
        StatusLine("Active voice", piper_server_active_voice_, theme),
        StatusLine("Uptime", piper_server_uptime_, theme),
        StatusLine("Requests", piper_server_requests_, theme),
        StatusLine("Address", address, theme),
    }, theme);

    Element settings_panel = Panel("Settings", {
        ServerInputRow("Port", piper_server_port_input_, "blank = 59123", theme),
        piper_server_cuda_checkbox_->Render() | size(WIDTH, EQUAL, 40),
        ServerInputRow("Noise scale", piper_server_noise_scale_input_, "0.45..0.85", theme),
        ServerInputRow("Sentence silence", piper_server_sentence_silence_input_, "0.00..0.50 sec", theme),
        ServerInputRow("Speaker ID", piper_server_speaker_id_input_, "0 default", theme),
    }, theme);

    return vbox({
        button_rows,
        separator() | color(theme.modal_border),
        hbox({
            status_panel,
            separator() | color(theme.modal_border),
            settings_panel,
        }),
        separator() | color(theme.modal_border),
        validation,
        paragraph(piper_server_status_) | color(theme.modal_text_color),
        text("TTS Test uses textlt-piper-server only. No direct Piper fallback.") |
            color(theme.modal_text_color) | dim,
    }) | border;
}
} // namespace textlt
