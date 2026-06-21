#include "assistant_modals.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <algorithm>
#include <utility>

#include <curl/curl.h>

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

size_t WriteFileCallback(char* data, size_t size, size_t count, void* user_data) {
    FILE* file = static_cast<FILE*>(user_data);
    return std::fwrite(data, size, count, file);
}

struct PiperDownloadContext {
    std::atomic_bool* cancel = nullptr;
    std::mutex* mutex = nullptr;
    unsigned long long* downloaded_bytes = nullptr;
    unsigned long long* total_bytes = nullptr;
    float* progress_ratio = nullptr;
    std::function<void()>* request_redraw = nullptr;
};

int PiperProgressCallback(void* client,
                          curl_off_t total,
                          curl_off_t downloaded,
                          curl_off_t,
                          curl_off_t) {
    auto* context = static_cast<PiperDownloadContext*>(client);
    if (!context || !context->cancel || !context->mutex) {
        return 0;
    }
    if (*context->cancel) {
        return 1;
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
    return 0;
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

    CreateDirectory(final_path.parent_path());
    CreateDirectory(DownloadCacheDirectory());
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

    FILE* file = std::fopen(part_path.string().c_str(), "wb");
    if (!file) {
        *error_message = "Could not create .part file";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        std::filesystem::remove(part_path, error);
        *error_message = "Could not initialize download";
        return false;
    }

    PiperDownloadContext context{
        &cancel,
        &state_mutex,
        &downloaded_bytes,
        &total_bytes,
        &progress_ratio,
        &request_redraw,
    };
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "textlt/1.0");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, PiperProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);

    const CURLcode result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    const bool close_ok = std::fclose(file) == 0;

    if (result != CURLE_OK || !close_ok) {
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

TtsModalContent::TtsModalContent(const Theme* theme)
    : theme_(theme) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
}

ftxui::Element TtsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        text(" Text-to-Speech") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        StatusLine("Language", "not selected", theme),
        StatusLine("Speaker", "not selected", theme),
        text(""),
        text(" Piper and voice are not configured.") |
            color(theme.modal_text_color),
        text(" Install/download/status controls will be added later.") |
            dim |
            color(theme.modal_text_color),
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

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

    std::lock_guard<std::mutex> lock(tts_download_mutex_);
    tts_status_ = "TODO: Piper test playback not implemented";
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
            tts_download_button_->Render(),
            text(" "),
            tts_delete_button_->Render(),
            text(" "),
            tts_test_button_->Render(),
        }),
    };
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
