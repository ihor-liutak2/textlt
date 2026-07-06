#include "assistant_modals.hpp"

#include <filesystem>
#include <map>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>

#include <archive.h>
#include <archive_entry.h>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "assistant_download_progress.hpp"
#include "piper_manager.hpp"

namespace textlt {
namespace {

ftxui::Element StatusLine(const std::string& label,
                          const std::string& value,
                          const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(" " + label + ": ") | bold | color(theme.modal_accent),
        text(value) | color(theme.modal_text_color),
    });
}

std::string SelectedPiperVoiceLabel(const std::vector<std::string>& labels,
                                    int selected_voice) {
    return selected_voice >= 0 && selected_voice < static_cast<int>(labels.size())
        ? labels[selected_voice]
        : "-";
}


bool IsSafeArchivePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool ExtractArchive(const std::filesystem::path& archive_path,
                    const std::filesystem::path& destination) {
    using namespace assistant_modal_detail;

    EnsureDirectory(destination);
    archive* input = archive_read_new();
    archive* output = archive_write_disk_new();
    if (!input || !output) {
        if (input) {
            archive_read_free(input);
        }
        if (output) {
            archive_write_free(output);
        }
        return false;
    }

    archive_read_support_filter_all(input);
    archive_read_support_format_all(input);
    archive_write_disk_set_options(
        output,
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_SYMLINKS);

    if (archive_read_open_filename(input, archive_path.string().c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(input);
        archive_write_free(output);
        return false;
    }

    bool ok = true;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(input, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname(entry);
        const std::filesystem::path relative_path = pathname ? pathname : "";
        if (!IsSafeArchivePath(relative_path)) {
            archive_read_data_skip(input);
            continue;
        }

        const std::filesystem::path full_path = destination / relative_path;
        const std::string full_path_string = full_path.string();
        archive_entry_set_pathname(entry, full_path_string.c_str());
        const int header_result = archive_write_header(output, entry);
        if (header_result != ARCHIVE_OK && header_result != ARCHIVE_WARN) {
            ok = false;
            break;
        }

        const void* buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (archive_entry_size(entry) > 0) {
            const int block_result = archive_read_data_block(input, &buffer, &size, &offset);
            if (block_result == ARCHIVE_EOF) {
                break;
            }
            if (block_result != ARCHIVE_OK ||
                archive_write_data_block(output, buffer, size, offset) != ARCHIVE_OK) {
                ok = false;
                break;
            }
        }
        if (!ok || archive_write_finish_entry(output) != ARCHIVE_OK) {
            ok = false;
            break;
        }
    }

    archive_read_close(input);
    archive_read_free(input);
    archive_write_close(output);
    archive_write_free(output);

#ifndef _WIN32
    const std::filesystem::path piper = PiperManager::RuntimeExecutablePath();
    if (!piper.empty()) {
        std::error_code permissions_error;
        std::filesystem::permissions(
            piper,
            std::filesystem::perms::owner_exec |
                std::filesystem::perms::group_exec |
                std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add,
            permissions_error);
    }
#endif
    return ok;
}

bool DownloadRuntimeArchive(const std::string& url,
                            const std::filesystem::path& final_path,
                            std::mutex& state_mutex) {
    using namespace assistant_modal_detail;

    EnsureDirectory(final_path.parent_path());
    const std::filesystem::path part_path = final_path.string() + ".part";
    std::error_code error;
    std::filesystem::remove(part_path, error);

    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&state_mutex](unsigned long long, unsigned long long) {
            std::lock_guard<std::mutex> lock(state_mutex);
            return true;
        });

    if (!download_ok) {
        std::filesystem::remove(part_path, error);
        return false;
    }

    std::filesystem::rename(part_path, final_path, error);
    if (error) {
        std::filesystem::remove(final_path, error);
        error.clear();
        std::filesystem::rename(part_path, final_path, error);
    }
    if (error) {
        std::filesystem::remove(part_path, error);
        return false;
    }
    return true;
}

bool PlayWaveFile(const std::filesystem::path& wav_path, std::string* error) {
#ifdef _WIN32
    const std::string command =
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \"$p = " +
        PiperManager::QuotePowerShellSingle(wav_path) +
        "; Add-Type -AssemblyName System; (New-Object System.Media.SoundPlayer $p).PlaySync()\" > NUL 2>&1";
    const int result = std::system(command.c_str());
    if (result != 0 && error) {
        *error = "Windows SoundPlayer failed";
    }
    return result == 0;
#else
    const std::vector<std::string> players = {"paplay", "pw-play", "aplay", "ffplay"};
    for (const std::string& player : players) {
        const std::string probe = "command -v " + player + " >/dev/null 2>&1";
        if (std::system(probe.c_str()) != 0) {
            continue;
        }
        std::string command;
        if (player == "ffplay") {
            command = player + " -nodisp -autoexit " + PiperManager::QuoteShellPath(wav_path) + " >/dev/null 2>&1";
        } else {
            command = player + " " + PiperManager::QuoteShellPath(wav_path) + " >/dev/null 2>&1";
        }
        if (std::system(command.c_str()) == 0) {
            return true;
        }
    }
    if (error) {
        *error = "No audio player found. Install paplay, pw-play, aplay, or ffplay.";
    }
    return false;
#endif
}


bool FindSelectedPiperVoice(const std::string& selected_language,
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

std::string SelectedPiperLanguage(const std::vector<std::string>& labels,
                                  int selected_language) {
    return selected_language >= 0 &&
            selected_language < static_cast<int>(labels.size())
        ? labels[selected_language]
        : "";
}

int SelectedPiperVoiceCount(const std::vector<std::string>& labels,
                            int selected_voice) {
    if (selected_voice < 0 ||
        selected_voice >= static_cast<int>(labels.size()) ||
        labels[selected_voice] == "No voices") {
        return 0;
    }
    return 1;
}

std::vector<Json> SelectedInstalledPiperVoices(const std::string& selected_language,
                                               int selected_voice,
                                               const std::vector<std::string>& labels) {
    std::vector<Json> selected;
    if (SelectedPiperVoiceCount(labels, selected_voice) == 0) {
        return selected;
    }

    Json voice;
    if (FindSelectedPiperVoice(selected_language, selected_voice, &voice) &&
        PiperManager::VoiceInstalled(voice)) {
        selected.push_back(std::move(voice));
    }
    return selected;
}

bool DownloadPiperFile(const std::string& url,
                       const std::filesystem::path& final_path,
                       const std::string& display_name,
                       std::mutex& state_mutex,
                       std::atomic_bool& cancel,
                       std::string& current_file,
                       std::string* error_message) {
    using namespace assistant_modal_detail;

    EnsureDirectory(final_path.parent_path());
    EnsureDirectory(DownloadCacheDirectory());
    const std::filesystem::path part_path =
        DownloadCacheDirectory() / (final_path.filename().string() + ".part");

    std::error_code error;
    std::filesystem::remove(part_path, error);

    {
        std::lock_guard<std::mutex> lock(state_mutex);
        current_file = display_name;
    }
    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&cancel, &state_mutex](unsigned long long, unsigned long long) {
            if (cancel.load()) {
                return false;
            }
            std::lock_guard<std::mutex> lock(state_mutex);
            return true;
        });

    if (!download_ok) {
        std::filesystem::remove(part_path, error);
        *error_message = cancel.load()
            ? "Download cancelled"
            : "Download failed";
        return false;
    }

    std::filesystem::rename(part_path, final_path, error);
    if (error) {
        std::filesystem::remove(final_path, error);
        error.clear();
        std::filesystem::rename(part_path, final_path, error);
    }
    if (error) {
        std::filesystem::remove(part_path, error);
        *error_message = "Could not save downloaded file";
        return false;
    }
    return true;
}

} // namespace

void AssistantSettingsModalContent::LoadPiperRegistry() {
    using namespace assistant_modal_detail;

    Json root;
    const RegistryLoadResult load_result = LoadUserRegistryJson(RegistryKind::Piper, &root);
    std::map<std::string, std::vector<std::string>> voices_by_language;
    if (load_result == RegistryLoadResult::Loaded) {
        const auto voices = root.find("voices");
        if (voices != root.end() && voices->is_array()) {
            for (const Json& voice : *voices) {
                if (!voice.is_object()) {
                    continue;
                }
                const std::string code = JsonString(voice, "language_code");
                const std::string language = JsonString(voice, "language_name", code);
                const std::string country = JsonString(voice, "country");
                const std::string key = country.empty() ? language : language + " - " + country;
                voices_by_language[key].push_back("");
            }
        }
    }

    tts_language_labels_.clear();
    for (const auto& entry : voices_by_language) {
        tts_language_labels_.push_back(entry.first);
    }
    if (tts_language_labels_.empty()) {
        tts_language_labels_.push_back("No languages");
        if (load_result == RegistryLoadResult::Missing) {
            tts_status_ = "Registry not loaded";
        } else if (load_result == RegistryLoadResult::ParseFailed) {
            tts_status_ = "Failed to parse registry";
        } else {
            tts_status_ = "Registry loaded, no items found";
        }
    } else if (tts_status_.find("TODO:") != 0) {
        tts_status_ = "Registry loaded";
    }
    selected_tts_language_ = 0;
    RebuildTtsVoices();
}

void AssistantSettingsModalContent::RebuildTtsVoices() {
    using namespace assistant_modal_detail;

    tts_delete_confirm_visible_ = false;
    tts_delete_pending_voices_.clear();

    Json root;
    const RegistryLoadResult load_result = LoadUserRegistryJson(RegistryKind::Piper, &root);
    const std::string selected_language =
        selected_tts_language_ >= 0 &&
                selected_tts_language_ < static_cast<int>(tts_language_labels_.size())
            ? tts_language_labels_[selected_tts_language_]
            : "";
    tts_voice_labels_.clear();
    if (load_result == RegistryLoadResult::Loaded) {
        const auto voices = root.find("voices");
        if (voices != root.end() && voices->is_array()) {
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
                std::string label = JsonString(voice, "id");
                const std::string quality = JsonString(voice, "quality");
                label += " | " + (quality.empty() ? "unknown" : quality);
                label += PiperManager::VoiceInstalled(voice) ? " | installed" : " | not installed";
                tts_voice_labels_.push_back(label);
            }
        }
    }
    if (tts_voice_labels_.empty()) {
        tts_voice_labels_.push_back("No voices");
    }
    selected_tts_voice_ = 0;
}


void AssistantSettingsModalContent::StartPiperRuntimeInstall() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        if (tts_downloading_) {
            tts_status_ = "TTS download is running";
            return;
        }
    }
    if (tts_runtime_thread_.joinable()) {
        tts_runtime_thread_.join();
    }

    const std::string url = PiperManager::RuntimeDownloadUrl();
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Piper runtime is not available for this platform";
        return;
    }

    const std::filesystem::path archive_path = PiperManager::RuntimeDownloadArchivePath();
    const std::filesystem::path runtime_directory = PiperManager::RuntimeDirectory();
    if (archive_path.empty() || runtime_directory.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Piper install path is not available";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_cancel_download_ = false;
        tts_downloading_ = true;
        tts_download_visible_ = true;
        tts_refresh_after_download_ = false;
        tts_delete_confirm_visible_ = false;
        tts_download_current_file_ = archive_path.filename().string();
        tts_progress_ratio_ = 0.0f;
        tts_status_ = "Downloading Piper runtime...";
    }
    RequestRedraw();

    tts_runtime_thread_ = std::thread([this, url, archive_path, runtime_directory] {
        std::atomic_bool progress_running{true};
        std::thread progress_thread = assistant_modal_detail::StartAssistantDownloadProgress(
            progress_running,
            [this](float progress) {
                std::lock_guard<std::mutex> lock(tts_download_mutex_);
                tts_progress_ratio_ = progress;
            },
            request_redraw_);

        const bool download_ok = DownloadRuntimeArchive(
            url,
            archive_path,
            tts_download_mutex_);

        progress_running = false;
        if (progress_thread.joinable()) {
            progress_thread.join();
        }
        if (!download_ok) {
            std::lock_guard<std::mutex> lock(tts_download_mutex_);
            tts_downloading_ = false;
            tts_download_visible_ = false;
            tts_status_ = "Piper runtime download failed";
            tts_progress_ratio_ = 0.0f;
            RequestRedraw();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(tts_download_mutex_);
            tts_status_ = "Extracting Piper runtime...";
            tts_download_current_file_ = "extracting " + archive_path.filename().string();
            tts_progress_ratio_ = 0.0f;
        }
        RequestRedraw();

        const bool extract_ok = ExtractArchive(archive_path, runtime_directory);
        for (int step = 1; step <= 60; ++step) {
            {
                std::lock_guard<std::mutex> lock(tts_download_mutex_);
                tts_progress_ratio_ = static_cast<float>(step) / 60.0f;
            }
            RequestRedraw();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        const bool installed = extract_ok && PiperManager::RuntimeInstalled();
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_downloading_ = false;
        tts_download_visible_ = false;
        tts_progress_ratio_ = installed ? 1.0f : 0.0f;
        tts_status_ = installed ? "Piper runtime installed" : "Piper runtime install failed";
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::StartTtsVoiceDownload() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        if (tts_downloading_) {
            return;
        }
    }
    if (tts_download_thread_.joinable()) {
        tts_download_thread_.join();
    }

    const std::string selected_language =
        selected_tts_language_ >= 0 &&
                selected_tts_language_ < static_cast<int>(tts_language_labels_.size())
            ? tts_language_labels_[selected_tts_language_]
            : "";

    Json voice;
    if (!FindSelectedPiperVoice(selected_language, selected_tts_voice_, &voice)) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Select a voice first";
        tts_download_visible_ = false;
        return;
    }

    const std::string model_url = JsonString(voice, "model_url");
    const std::string config_url = JsonString(voice, "config_url");
    if (model_url.empty() || config_url.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Voice registry entry is incomplete";
        tts_download_visible_ = false;
        return;
    }

    const std::filesystem::path model_final_path = PiperManager::VoiceModelPath(voice);
    const std::filesystem::path config_final_path = PiperManager::VoiceConfigPath(voice);
    if (model_final_path.empty() || config_final_path.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Voice registry entry has invalid local paths";
        tts_download_visible_ = false;
        return;
    }
    const std::string model_filename = model_final_path.filename().string();
    const std::string config_filename = config_final_path.filename().string();

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_cancel_download_ = false;
        tts_downloading_ = true;
        tts_download_visible_ = true;
        tts_refresh_after_download_ = false;
        tts_download_current_file_ = model_filename;
        tts_progress_ratio_ = 0.0f;
        tts_status_ = "Downloading voice...";
    }
    RequestRedraw();

    tts_download_thread_ = std::thread([this,
                                        model_url,
                                        config_url,
                                        model_final_path,
                                        config_final_path,
                                        model_filename,
                                        config_filename] {
        std::atomic_bool progress_running{true};
        std::thread progress_thread = assistant_modal_detail::StartAssistantDownloadProgress(
            progress_running,
            [this](float progress) {
                std::lock_guard<std::mutex> lock(tts_download_mutex_);
                tts_progress_ratio_ = progress;
            },
            request_redraw_);

        std::string error_message;
        const bool model_ok = DownloadPiperFile(
            model_url,
            model_final_path,
            model_filename,
            tts_download_mutex_,
            tts_cancel_download_,
            tts_download_current_file_,
            &error_message);
        bool config_ok = false;
        if (model_ok) {
            config_ok = DownloadPiperFile(
                config_url,
                config_final_path,
                config_filename,
                tts_download_mutex_,
                tts_cancel_download_,
                tts_download_current_file_,
                &error_message);
        }

        progress_running = false;
        if (progress_thread.joinable()) {
            progress_thread.join();
        }

        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_downloading_ = false;
        if (model_ok && config_ok) {
            tts_status_ = "Voice downloaded";
            tts_download_visible_ = false;
            tts_progress_ratio_ = 1.0f;
            tts_refresh_after_download_ = true;
        } else {
            tts_download_visible_ = true;
            tts_status_ = error_message.empty()
                ? "Voice download failed"
                : "Voice download failed: " + error_message;
        }
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::StartTtsVoiceDelete() {
    const std::string selected_language =
        SelectedPiperLanguage(tts_language_labels_, selected_tts_language_);
    std::vector<Json> voices =
        SelectedInstalledPiperVoices(selected_language, selected_tts_voice_, tts_voice_labels_);
    if (voices.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_confirm_visible_ = false;
        tts_delete_pending_voices_.clear();
        tts_status_ = "No installed voice selected";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_pending_voices_ = std::move(voices);
        tts_delete_confirm_visible_ = true;
        tts_download_visible_ = false;
    }
    if (tts_confirm_delete_button_) {
        tts_confirm_delete_button_->TakeFocus();
    }
}

void AssistantSettingsModalContent::ConfirmTtsVoiceDelete() {
    using namespace assistant_modal_detail;

    std::vector<Json> voices;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        voices = tts_delete_pending_voices_;
    }
    if (voices.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_confirm_visible_ = false;
        tts_status_ = "No installed voice selected";
        return;
    }

    for (const Json& voice : voices) {
        std::error_code error;
        for (const std::filesystem::path& model_path : PiperManager::VoiceModelPathCandidates(voice)) {
            std::filesystem::remove(model_path, error);
            error.clear();
        }
        for (const std::filesystem::path& config_path : PiperManager::VoiceConfigPathCandidates(voice)) {
            std::filesystem::remove(config_path, error);
            error.clear();
        }
        const std::filesystem::path voice_directory = PiperManager::VoiceDirectory(voice);
        if (!voice_directory.empty()) {
            std::filesystem::remove(voice_directory, error);
        }
    }

    const size_t deleted_count = voices.size();
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_delete_confirm_visible_ = false;
        tts_delete_pending_voices_.clear();
    }
    RebuildTtsVoices();
    selected_tts_voice_ = -1;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = deleted_count == 1 ? "Voice deleted" : "Voices deleted";
    }
}

void AssistantSettingsModalContent::CancelTtsVoiceDelete() {
    std::lock_guard<std::mutex> lock(tts_download_mutex_);
    tts_delete_confirm_visible_ = false;
    tts_delete_pending_voices_.clear();
}

void AssistantSettingsModalContent::TestTtsVoice() {
    const int selected_count =
        SelectedPiperVoiceCount(tts_voice_labels_, selected_tts_voice_);
    if (selected_count == 0) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "No voice selected";
        return;
    }
    if (selected_count > 1) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Select one voice to test";
        return;
    }
    if (!PiperManager::RuntimeInstalled()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Piper runtime is not installed";
        return;
    }

    Json voice;
    const std::string selected_language =
        SelectedPiperLanguage(tts_language_labels_, selected_tts_language_);
    if (!FindSelectedPiperVoice(selected_language, selected_tts_voice_, &voice)) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "No voice selected";
        return;
    }
    if (!PiperManager::VoiceInstalled(voice)) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Voice is not installed";
        return;
    }

    if (tts_runtime_thread_.joinable()) {
        tts_runtime_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        if (tts_downloading_) {
            tts_status_ = "TTS task is running";
            return;
        }
        tts_downloading_ = true;
        tts_download_visible_ = false;
        tts_status_ = "Generating Piper test audio...";
    }
    const std::string test_text =
        "Hello. This is a test of the local Piper voice in TextLT.";
    ShowTtsTestPopup(test_text);
    RequestRedraw();

    tts_runtime_thread_ = std::thread([this, voice, test_text] {
        const std::filesystem::path test_directory =
            PiperManager::ModelsDirectory().parent_path() / "test";
        assistant_modal_detail::EnsureDirectory(test_directory);
        const std::filesystem::path wav_path = test_directory / "piper_test.wav";
        std::string error_message;
        const bool generated = GeneratePiperTestAudio(
            voice,
            test_text,
            wav_path,
            &error_message);
        bool played = false;
        if (generated) {
            played = PlayWaveFile(wav_path, &error_message);
        }

        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_downloading_ = false;
        if (generated && played) {
            tts_status_ = "Piper test played";
        } else if (generated) {
            tts_status_ = error_message.empty()
                ? "Piper test audio generated, playback failed"
                : "Piper test audio generated, playback failed: " + error_message;
        } else {
            tts_status_ = error_message.empty()
                ? "Piper test failed"
                : "Piper test failed: " + error_message;
        }
        RequestRedraw();
    });
}

void AssistantSettingsModalContent::ShowTtsTestPopup(std::string text) {
    tts_test_popup_text_ = std::move(text);
    tts_test_popup_visible_ = true;
    popup_layer_index_ = 1;
    if (tts_test_popup_close_button_) {
        tts_test_popup_close_button_->TakeFocus();
    }
    RequestRedraw();
}

void AssistantSettingsModalContent::CloseTtsTestPopup() {
    tts_test_popup_visible_ = false;
    popup_layer_index_ = 0;
    tts_test_popup_text_.clear();
    RequestRedraw();
}

void AssistantSettingsModalContent::ApplyTtsDownloadCompletion() {
    bool should_refresh = false;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        should_refresh = tts_refresh_after_download_;
        tts_refresh_after_download_ = false;
    }
    if (should_refresh) {
        RebuildTtsVoices();
    }
}

void AssistantSettingsModalContent::RequestRedraw() const {
    if (request_redraw_) {
        request_redraw_();
    }
}

ftxui::Element AssistantSettingsModalContent::RenderTtsTestPopup(const Theme& theme) const {
    using namespace ftxui;

    return vbox({
        text(" Piper test text ") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        paragraph(tts_test_popup_text_.empty()
                      ? std::string("No test text available.")
                      : tts_test_popup_text_) |
            color(theme.modal_text_color) |
            frame |
            vscroll_indicator |
            size(HEIGHT, LESS_THAN, 10),
        separator() | color(theme.modal_border),
        hbox({filler(), tts_test_popup_close_button_->Render()}),
    }) |
        border |
        bgcolor(theme.modal_background) |
        color(theme.modal_foreground) |
        size(WIDTH, LESS_THAN, 72) |
        clear_under;
}

ftxui::Element AssistantSettingsModalContent::RenderTtsTab(const Theme& theme) {
    using namespace ftxui;
    ApplyTtsDownloadCompletion();

    bool show_download_progress = false;
    bool show_delete_confirmation = false;
    std::string current_file;
    float progress_ratio = 0.0f;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        show_download_progress = tts_download_visible_;
        show_delete_confirmation = tts_delete_confirm_visible_;
        current_file = tts_download_current_file_;
        progress_ratio = tts_progress_ratio_;
    }

    Elements rows = {
        hbox({
            fetch_tts_button_->Render(),
            text(" "),
            tts_runtime_install_button_->Render(),
            text(" "),
            tts_download_button_->Render(),
            text(" "),
            tts_delete_button_->Render(),
            text(" "),
            tts_test_button_->Render(),
        }),
    };
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(StatusLine(
        "Piper runtime",
        PiperManager::RuntimeInstalled() ? "installed" : "missing - use Install Piper",
        theme));
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" Language") | bold | color(theme.modal_text_color));
    rows.push_back(tts_language_menu_->Render() | border);
    if (show_download_progress) {
        progress_ratio = std::max(0.0f, std::min(1.0f, progress_ratio));
        rows.push_back(separator() | color(theme.modal_border));
        rows.push_back(text(" Download progress") | bold | color(theme.modal_text_color));
        rows.push_back(text(" " + current_file) | color(theme.modal_text_color));
        const int percent =
            static_cast<int>(progress_ratio * 100.0f + 0.5f);
        rows.push_back(hbox({
            filler(),
            text(std::to_string(percent) + "% ") |
                color(theme.modal_text_color),
        }));
        rows.push_back(gauge(progress_ratio) | border);
    }
    if (show_delete_confirmation) {
        rows.push_back(separator() | color(theme.modal_border));
        rows.push_back(text(" Delete selected voice files?") |
                       bold |
                       color(theme.modal_text_color));
        rows.push_back(hbox({
            tts_confirm_delete_button_->Render(),
            text(" "),
            tts_cancel_delete_button_->Render(),
        }));
    }
    rows.push_back(text(" Voices") | bold | color(theme.modal_text_color));
    rows.push_back(StatusLine(
        "Selected voice",
        SelectedPiperVoiceLabel(tts_voice_labels_, selected_tts_voice_),
        theme));
    rows.push_back(tts_voice_menu_->Render() | border);
    Element body = vbox(std::move(rows)) | border;
    if (!tts_test_popup_visible_) {
        return body;
    }

    return dbox({
        body,
        vbox({
            filler(),
            hbox({
                filler(),
                RenderTtsTestPopup(theme),
                filler(),
            }),
            filler(),
        }),
    });
}

} // namespace textlt
