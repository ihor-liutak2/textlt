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

bool PiperVoiceInstalled(const Json& voice) {
    using namespace assistant_modal_detail;

    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    if (model_path.empty() || config_path.empty()) {
        return false;
    }

    const std::filesystem::path models_directory =
        UserDataDirectory() / "piper" / "models";
    std::error_code error;
    return std::filesystem::exists(models_directory / model_path, error) &&
           std::filesystem::exists(models_directory / config_path, error);
}

std::string FormatBytes(unsigned long long value) {
    std::string text = std::to_string(value);
    std::string formatted;
    int group_count = 0;
    for (auto iter = text.rbegin(); iter != text.rend(); ++iter) {
        if (group_count == 3) {
            formatted.push_back(' ');
            group_count = 0;
        }
        formatted.push_back(*iter);
        ++group_count;
    }
    std::reverse(formatted.begin(), formatted.end());
    return formatted;
}


std::filesystem::path PiperRuntimeDirectory() {
    using namespace assistant_modal_detail;
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "piper" / "bin";
}

std::filesystem::path PiperModelsDirectory() {
    using namespace assistant_modal_detail;
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "piper" / "models";
}

std::filesystem::path PiperDownloadArchivePath() {
    using namespace assistant_modal_detail;
#ifdef _WIN32
    const std::string filename = "piper_windows_amd64.zip";
#else
    const std::string filename = "piper_linux_x86_64.tar.gz";
#endif
    const std::filesystem::path cache = DownloadCacheDirectory();
    return cache.empty() ? std::filesystem::path{} : cache / filename;
}

std::string PiperRuntimeDownloadUrl() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip";
#elif !defined(_WIN32) && defined(__x86_64__)
    return "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz";
#else
    return {};
#endif
}

std::filesystem::path FindPiperExecutable() {
    const std::filesystem::path runtime_directory = PiperRuntimeDirectory();
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

bool PiperRuntimeInstalled() {
    return !FindPiperExecutable().empty();
}

std::string QuoteShellPath(const std::filesystem::path& path) {
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

std::string QuotePowerShellSingle(const std::filesystem::path& path) {
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
    const std::filesystem::path piper = FindPiperExecutable();
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
                            std::mutex& state_mutex,
                            unsigned long long& downloaded_bytes,
                            unsigned long long& total_bytes,
                            float& progress_ratio,
                            const std::function<void()>& request_redraw) {
    using namespace assistant_modal_detail;

    EnsureDirectory(final_path.parent_path());
    const std::filesystem::path part_path = final_path.string() + ".part";
    std::error_code error;
    std::filesystem::remove(part_path, error);

    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&](unsigned long long total, unsigned long long downloaded) {
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                downloaded_bytes = downloaded;
                if (total > 0) {
                    total_bytes = total;
                    progress_ratio = static_cast<float>(downloaded) / static_cast<float>(total);
                } else if (downloaded > 0) {
                    progress_ratio = 0.05f;
                }
            }
            if (request_redraw) {
                request_redraw();
            }
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
        QuotePowerShellSingle(wav_path) +
        "; Add-Type -AssemblyName System; (New-Object System.Media.SoundPlayer $p).PlaySync()\"";
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
            command = player + " -nodisp -autoexit " + QuoteShellPath(wav_path) + " >/dev/null 2>&1";
        } else {
            command = player + " " + QuoteShellPath(wav_path) + " >/dev/null 2>&1";
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

bool RunPiperToFile(const Json& voice,
                    const std::string& text,
                    const std::filesystem::path& output_wav,
                    std::string* error) {
    using namespace assistant_modal_detail;

    const std::filesystem::path executable = FindPiperExecutable();
    if (executable.empty()) {
        if (error) {
            *error = "Piper runtime is not installed";
        }
        return false;
    }

    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    const std::filesystem::path model = PiperModelsDirectory() / model_path;
    const std::filesystem::path config = PiperModelsDirectory() / config_path;
    std::error_code exists_error;
    if (!std::filesystem::exists(model, exists_error) ||
        !std::filesystem::exists(config, exists_error)) {
        if (error) {
            *error = "Selected Piper voice files are missing";
        }
        return false;
    }

    EnsureDirectory(output_wav.parent_path());
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
        " --output_file " + QuoteShellPath(output_wav);
#else
    const std::string command =
        QuoteShellPath(executable) +
        " --model " + QuoteShellPath(model) +
        " --config " + QuoteShellPath(config) +
        " --output_file " + QuoteShellPath(output_wav) +
        " < " + QuoteShellPath(input_path);
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
    if (!std::filesystem::exists(output_wav, exists_error)) {
        if (error) {
            *error = "Piper did not create audio file";
        }
        return false;
    }
    return true;
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
        PiperVoiceInstalled(voice)) {
        selected.push_back(std::move(voice));
    }
    return selected;
}

struct PiperDownloadContext {
    std::atomic_bool* cancel = nullptr;
    std::mutex* mutex = nullptr;
    unsigned long long* downloaded_bytes = nullptr;
    unsigned long long* total_bytes = nullptr;
    float* progress_ratio = nullptr;
    std::function<void()>* request_redraw = nullptr;
};

bool UpdatePiperProgress(PiperDownloadContext* context,
                         unsigned long long total,
                         unsigned long long downloaded) {
    if (!context || !context->cancel || !context->mutex) {
        return true;
    }
    if (*context->cancel) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(*context->mutex);
        *context->downloaded_bytes =
            downloaded > 0 ? static_cast<unsigned long long>(downloaded) : 0;
        if (total > 0) {
            *context->total_bytes = static_cast<unsigned long long>(total);
            *context->progress_ratio =
                static_cast<float>(downloaded) / static_cast<float>(total);
        } else if (*context->total_bytes > 0) {
            *context->progress_ratio =
                static_cast<float>(*context->downloaded_bytes) /
                static_cast<float>(*context->total_bytes);
        } else {
            *context->progress_ratio = downloaded > 0 ? 0.05f : 0.0f;
        }
    }
    if (context->request_redraw && *context->request_redraw) {
        (*context->request_redraw)();
    }
    return true;
}

bool DownloadPiperFile(const std::string& url,
                       const std::filesystem::path& final_path,
                       const std::string& display_name,
                       unsigned long long expected_size,
                       std::mutex& state_mutex,
                       std::atomic_bool& cancel,
                       std::string& current_file,
                       unsigned long long& downloaded_bytes,
                       unsigned long long& total_bytes,
                       float& progress_ratio,
                       std::function<void()>& request_redraw,
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
        downloaded_bytes = 0;
        total_bytes = expected_size;
        progress_ratio = 0.0f;
    }
    if (request_redraw) {
        request_redraw();
    }

    PiperDownloadContext context{
        &cancel,
        &state_mutex,
        &downloaded_bytes,
        &total_bytes,
        &progress_ratio,
        &request_redraw,
    };
    const bool download_ok = CurlManager::DownloadToFile(
        url,
        part_path,
        [&context](unsigned long long total, unsigned long long downloaded) {
            return UpdatePiperProgress(&context, total, downloaded);
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
                label += PiperVoiceInstalled(voice) ? " | installed" : " | not installed";
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

    const std::string url = PiperRuntimeDownloadUrl();
    if (url.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Piper runtime is not available for this platform";
        return;
    }

    const std::filesystem::path archive_path = PiperDownloadArchivePath();
    const std::filesystem::path runtime_directory = PiperRuntimeDirectory();
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
        tts_downloaded_bytes_ = 0;
        tts_total_bytes_ = 0;
        tts_progress_ratio_ = 0.0f;
        tts_status_ = "Downloading Piper runtime...";
    }
    RequestRedraw();

    tts_runtime_thread_ = std::thread([this, url, archive_path, runtime_directory] {
        const bool download_ok = DownloadRuntimeArchive(
            url,
            archive_path,
            tts_download_mutex_,
            tts_downloaded_bytes_,
            tts_total_bytes_,
            tts_progress_ratio_,
            request_redraw_);
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
            tts_downloaded_bytes_ = 0;
            tts_total_bytes_ = 0;
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

        const bool installed = extract_ok && PiperRuntimeInstalled();
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

    const std::string model_path = JsonString(voice, "model_path");
    const std::string config_path = JsonString(voice, "config_path");
    const unsigned long long model_size =
        static_cast<unsigned long long>(JsonSize(voice, "model_size", 0));
    const unsigned long long config_size =
        static_cast<unsigned long long>(JsonSize(voice, "config_size", 0));
    if (model_path.empty() || config_path.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Voice registry entry is incomplete";
        tts_download_visible_ = false;
        return;
    }

    Json root;
    if (LoadUserRegistryJson(RegistryKind::Piper, &root) != RegistryLoadResult::Loaded) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Registry not loaded";
        tts_download_visible_ = false;
        return;
    }

    const std::string base_url = JsonString(root, "base_url");
    if (base_url.empty()) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "Registry has no base URL";
        tts_download_visible_ = false;
        return;
    }

    auto make_url = [base_url](const std::string& path) {
        if (base_url.back() == '/' || (!path.empty() && path.front() == '/')) {
            return base_url + path;
        }
        return base_url + "/" + path;
    };

    const std::filesystem::path models_directory =
        UserDataDirectory() / "piper" / "models";
    const std::filesystem::path model_final_path = models_directory / model_path;
    const std::filesystem::path config_final_path = models_directory / config_path;
    const std::string model_url = make_url(model_path);
    const std::string config_url = make_url(config_path);

    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_cancel_download_ = false;
        tts_downloading_ = true;
        tts_download_visible_ = true;
        tts_refresh_after_download_ = false;
        tts_download_current_file_ = std::filesystem::path(model_path).filename().string();
        tts_downloaded_bytes_ = 0;
        tts_total_bytes_ = 0;
        tts_progress_ratio_ = 0.0f;
        tts_status_ = "Downloading voice...";
    }
    RequestRedraw();

    tts_download_thread_ = std::thread([this,
                                        model_url,
                                        config_url,
                                        model_final_path,
                                        config_final_path,
                                        model_path,
                                        config_path,
                                        model_size,
                                        config_size] {
        std::string error_message;
        const bool model_ok = DownloadPiperFile(
            model_url,
            model_final_path,
            std::filesystem::path(model_path).filename().string(),
            model_size,
            tts_download_mutex_,
            tts_cancel_download_,
            tts_download_current_file_,
            tts_downloaded_bytes_,
            tts_total_bytes_,
            tts_progress_ratio_,
            request_redraw_,
            &error_message);
        bool config_ok = false;
        if (model_ok) {
            config_ok = DownloadPiperFile(
                config_url,
                config_final_path,
                std::filesystem::path(config_path).filename().string(),
                config_size,
                tts_download_mutex_,
                tts_cancel_download_,
                tts_download_current_file_,
                tts_downloaded_bytes_,
                tts_total_bytes_,
                tts_progress_ratio_,
                request_redraw_,
                &error_message);
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

    const std::filesystem::path models_directory =
        UserDataDirectory() / "piper" / "models";
    for (const Json& voice : voices) {
        const std::string model_path = JsonString(voice, "model_path");
        const std::string config_path = JsonString(voice, "config_path");
        std::error_code error;
        if (!model_path.empty()) {
            std::filesystem::remove(models_directory / model_path, error);
        }
        error.clear();
        if (!config_path.empty()) {
            std::filesystem::remove(models_directory / config_path, error);
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
    if (!PiperRuntimeInstalled()) {
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
    if (!PiperVoiceInstalled(voice)) {
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
    RequestRedraw();

    tts_runtime_thread_ = std::thread([this, voice] {
        const std::filesystem::path test_directory =
            PiperModelsDirectory().parent_path() / "test";
        assistant_modal_detail::EnsureDirectory(test_directory);
        const std::filesystem::path wav_path = test_directory / "piper_test.wav";
        std::string error_message;
        const bool generated = RunPiperToFile(
            voice,
            "Привіт. Це тест локального голосу Piper у TextLT.",
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

ftxui::Element AssistantSettingsModalContent::RenderTtsTab(const Theme& theme) {
    using namespace ftxui;
    ApplyTtsDownloadCompletion();

    bool show_download_progress = false;
    bool show_delete_confirmation = false;
    std::string current_file;
    unsigned long long downloaded_bytes = 0;
    unsigned long long total_bytes = 0;
    float progress_ratio = 0.0f;
    {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        show_download_progress = tts_download_visible_;
        show_delete_confirmation = tts_delete_confirm_visible_;
        current_file = tts_download_current_file_;
        downloaded_bytes = tts_downloaded_bytes_;
        total_bytes = tts_total_bytes_;
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
        PiperRuntimeInstalled() ? "installed" : "missing - use Install Piper",
        theme));
    const std::filesystem::path piper_executable = FindPiperExecutable();
    if (!piper_executable.empty()) {
        rows.push_back(StatusLine("Piper executable", piper_executable.string(), theme));
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(text(" Language") | bold | color(theme.modal_text_color));
    rows.push_back(tts_language_menu_->Render() | border);
    if (show_download_progress) {
        progress_ratio = std::max(0.0f, std::min(1.0f, progress_ratio));
        rows.push_back(separator() | color(theme.modal_border));
        rows.push_back(text(" Download progress") | bold | color(theme.modal_text_color));
        rows.push_back(text(" " + current_file) | color(theme.modal_text_color));
        std::string byte_text = FormatBytes(downloaded_bytes) + " bytes";
        if (total_bytes > 0) {
            byte_text = FormatBytes(downloaded_bytes) + " / " +
                        FormatBytes(total_bytes) + " bytes";
        }
        const int percent =
            static_cast<int>(progress_ratio * 100.0f + 0.5f);
        rows.push_back(hbox({
            text(" " + byte_text) | color(theme.modal_text_color),
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
    rows.push_back(tts_voice_menu_->Render() | border);
    return vbox(std::move(rows)) | border;
}

} // namespace textlt
