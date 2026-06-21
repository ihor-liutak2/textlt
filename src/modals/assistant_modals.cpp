#include "assistant_modals.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>

#include <curl/curl.h>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace assistant_modal_detail {

std::filesystem::path UserHomeDirectory() {
#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile);
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home);
#endif
}

std::filesystem::path UserDataDirectory() {
#ifdef _WIN32
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data && !std::string(local_app_data).empty()) {
        return std::filesystem::path(local_app_data) / "textlt";
    }
    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / "AppData" / "Local" / "textlt";
#else
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && !std::string(xdg_data_home).empty()) {
        return std::filesystem::path(xdg_data_home) / "textlt";
    }
    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".local" / "share" / "textlt";
#endif
}

std::filesystem::path DownloadCacheDirectory() {
#ifdef _WIN32
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "cache" / "downloads";
#else
    const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache_home && !std::string(xdg_cache_home).empty()) {
        return std::filesystem::path(xdg_cache_home) / "textlt" / "downloads";
    }
    const std::filesystem::path home = UserHomeDirectory();
    return home.empty() ? std::filesystem::path{} : home / ".cache" / "textlt" / "downloads";
#endif
}

std::filesystem::path RegistryDirectory() {
    const std::filesystem::path data = UserDataDirectory();
    return data.empty() ? std::filesystem::path{} : data / "registries";
}

void CreateDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path, error);
}

RegistryLoadResult LoadUserRegistryJson(const char* filename, Json* root) {
    const std::filesystem::path user_path = RegistryDirectory() / filename;
    std::error_code error;
    if (!std::filesystem::exists(user_path, error)) {
        *root = Json::object();
        return RegistryLoadResult::Missing;
    }

    std::ifstream file(user_path, std::ios::binary);
    if (!file) {
        *root = Json::object();
        return RegistryLoadResult::ParseFailed;
    }

    Json parsed = Json::parse(file, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        *root = Json::object();
        return RegistryLoadResult::ParseFailed;
    }

    *root = std::move(parsed);
    return RegistryLoadResult::Loaded;
}

std::string JsonLabel(const Json& object, const char* primary, const char* fallback) {
    const std::string value = JsonString(object, primary);
    return value.empty() ? JsonString(object, fallback) : value;
}

std::string BracketLabel(const std::string& label) {
    return !label.empty() && label.front() == '[' ? label : "[" + label + "]";
}

namespace {

size_t WriteFileCallback(char* data, size_t size, size_t count, void* user_data) {
    FILE* file = static_cast<FILE*>(user_data);
    return std::fwrite(data, size, count, file);
}

bool IsValidJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    const Json parsed = Json::parse(file, nullptr, false);
    return !parsed.is_discarded() && parsed.is_object();
}

} // namespace

RegistryDownloadResult DownloadRegistry(const char* url, const char* filename) {
    const std::filesystem::path registry_directory = RegistryDirectory();
    const std::filesystem::path cache_directory = DownloadCacheDirectory();
    if (registry_directory.empty() || cache_directory.empty()) {
        return RegistryDownloadResult::Failed;
    }

    CreateDirectory(registry_directory);
    CreateDirectory(cache_directory);

    const std::filesystem::path final_path = registry_directory / filename;
    const std::filesystem::path part_path = cache_directory / (std::string(filename) + ".part");
    std::error_code error;
    std::filesystem::remove(part_path, error);

    FILE* file = std::fopen(part_path.string().c_str(), "wb");
    if (!file) {
        return RegistryDownloadResult::Failed;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        std::filesystem::remove(part_path, error);
        return RegistryDownloadResult::Failed;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "textlt/1.0");

    const CURLcode result = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    const bool close_ok = std::fclose(file) == 0;

    if (result != CURLE_OK || !close_ok || response_code >= 400) {
        std::filesystem::remove(part_path, error);
        return RegistryDownloadResult::Failed;
    }

    const uintmax_t file_size = std::filesystem::file_size(part_path, error);
    if (error || file_size == 0) {
        std::filesystem::remove(part_path, error);
        return RegistryDownloadResult::Empty;
    }

    if (!IsValidJsonFile(part_path)) {
        std::filesystem::remove(part_path, error);
        return RegistryDownloadResult::InvalidJson;
    }

    std::filesystem::rename(part_path, final_path, error);
    if (error) {
        std::filesystem::remove(final_path, error);
        error.clear();
        std::filesystem::rename(part_path, final_path, error);
    }
    if (error) {
        std::filesystem::remove(part_path, error);
        return RegistryDownloadResult::Failed;
    }

    return RegistryDownloadResult::Saved;
}

} // namespace assistant_modal_detail

AssistantSettingsModalContent::AssistantSettingsModalContent(
    const Theme* theme,
    std::function<void()> request_redraw)
    : theme_(theme),
      request_redraw_(std::move(request_redraw)) {
    auto make_button = [this](std::string label, std::function<void()> on_click) {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = std::move(label);
        option.on_click = std::move(on_click);
        option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ftxui::Element button =
                ftxui::text(assistant_modal_detail::BracketLabel(state.label));
            if (state.focused || state.active) {
                return button |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg);
            }
            return button | ftxui::color(theme.modal_accent);
        };
        return ftxui::Button(option);
    };
    auto make_tab_button = [this](std::string label, int tab_index) {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = std::move(label);
        option.on_click = [this, tab_index] { selected_tab_ = tab_index; };
        option.transform = [this, tab_index](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ftxui::Element tab =
                ftxui::text(assistant_modal_detail::BracketLabel(state.label));
            if (selected_tab_ == tab_index || state.focused || state.active) {
                return tab |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg) |
                    ftxui::bold;
            }
            return tab | ftxui::color(theme.foreground) | ftxui::dim;
        };
        return ftxui::Button(option);
    };

    tts_tab_button_ = make_tab_button("TTS", 0);
    ai_tab_button_ = make_tab_button("AI", 1);
    tab_buttons_ = ftxui::Container::Horizontal({
        tts_tab_button_,
        ai_tab_button_,
    });

    ftxui::MenuOption language_option = ftxui::MenuOption::Vertical();
    language_option.on_change = [this] { RebuildTtsVoices(); };
    tts_language_menu_ =
        ftxui::Menu(&tts_language_labels_, &selected_tts_language_, language_option);

    ftxui::MenuOption checkbox_option = ftxui::MenuOption::Vertical();
    checkbox_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element row = ftxui::text(std::string(state.active ? "[x] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return row |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return row | ftxui::color(theme.modal_text_color);
    };
    tts_voice_menu_ = ftxui::Menu(&tts_voice_labels_, &selected_tts_voice_, checkbox_option);
    ai_model_menu_ = ftxui::Menu(&ai_model_labels_, &selected_ai_model_, checkbox_option);

    fetch_tts_button_ = make_button("Fetch registry", [this] { FetchTtsRegistry(); });
    tts_download_button_ = make_button("Download", [this] { StartTtsVoiceDownload(); });
    tts_delete_button_ = make_button("Delete", [this] {
        SetTodoStatus("TTS voice delete");
    });
    tts_test_button_ = make_button("Test", [this] {
        SetTodoStatus("TTS voice test");
    });
    fetch_ai_button_ = make_button("Fetch registry", [this] { FetchAiRegistry(); });
    ai_runtime_download_button_ = make_button("Download AI runtime", [this] {
        SetTodoStatus("AI runtime download");
    });
    ai_model_download_button_ = make_button("Download model", [this] {
        SetTodoStatus("AI model download");
    });
    ai_delete_model_button_ = make_button("Delete model", [this] {
        SetTodoStatus("AI model delete");
    });

    tab_body_container_ = ftxui::Container::Tab({
        ftxui::Container::Vertical({
            ftxui::Container::Horizontal({
                fetch_tts_button_,
                tts_download_button_,
                tts_delete_button_,
                tts_test_button_,
            }),
            tts_language_menu_,
            tts_voice_menu_,
        }),
        ftxui::Container::Vertical({
            ftxui::Container::Horizontal({
                fetch_ai_button_,
                ai_runtime_download_button_,
                ai_model_download_button_,
                ai_delete_model_button_,
            }),
            ai_model_menu_,
        }),
    }, &selected_tab_);

    container_ = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });

    LoadRegistries();
}

AssistantSettingsModalContent::~AssistantSettingsModalContent() {
    tts_cancel_download_ = true;
    if (tts_download_thread_.joinable()) {
        tts_download_thread_.join();
    }
}

ftxui::Element AssistantSettingsModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return (selected_tab_ == 0 ? RenderTtsTab(theme) : RenderAiTab(theme)) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
}

ftxui::Element AssistantSettingsModalContent::RenderTitle() {
    using namespace ftxui;
    return hbox({
        tts_tab_button_->Render(),
        text(" "),
        ai_tab_button_->Render(),
    });
}

std::string AssistantSettingsModalContent::GetFooterText() const {
    if (selected_tab_ == 0) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        return tts_status_;
    }
    return ai_status_;
}

void AssistantSettingsModalContent::LoadRegistries() {
    LoadPiperRegistry();
    LoadAiRegistry();
}

void AssistantSettingsModalContent::SetTodoStatus(std::string action) {
    using namespace assistant_modal_detail;

    if (selected_tab_ == 0) {
        CreateDirectory(UserDataDirectory() / "piper" / "models");
        CreateDirectory(DownloadCacheDirectory());
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        tts_status_ = "TODO: " + action + " support is not implemented";
        tts_progress_ = 0.0f;
        return;
    }
    CreateDirectory(UserDataDirectory() / "ai" / "models");
    CreateDirectory(UserDataDirectory() / "ai" / "runtimes");
    CreateDirectory(DownloadCacheDirectory());
    ai_status_ = "TODO: " + action + " support is not implemented";
    ai_progress_ = 0.0f;
}

TtsModal::TtsModal(const Theme* theme)
    : theme_(theme) {
    content_ = std::make_shared<TtsModalContent>(theme_);
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Placeholder only. Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component TtsModal::View() const {
    return modal_;
}

void TtsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void TtsModal::Close() {
    open_ = false;
}

bool TtsModal::IsOpen() const {
    return open_;
}

bool TtsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

AiActionsModal::AiActionsModal(const Theme* theme)
    : theme_(theme) {
    content_ = std::make_shared<AiActionsModalContent>(theme_);
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Placeholder only. Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AiActionsModal::View() const {
    return modal_;
}

void AiActionsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void AiActionsModal::Close() {
    open_ = false;
}

bool AiActionsModal::IsOpen() const {
    return open_;
}

bool AiActionsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

AssistantSettingsModal::AssistantSettingsModal(
    const Theme* theme,
    std::function<void()> request_redraw)
    : theme_(theme) {
    content_ = std::make_shared<AssistantSettingsModalContent>(
        theme_,
        std::move(request_redraw));
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Placeholder only. Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AssistantSettingsModal::View() const {
    return modal_;
}

void AssistantSettingsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void AssistantSettingsModal::Close() {
    open_ = false;
}

bool AssistantSettingsModal::IsOpen() const {
    return open_;
}

bool AssistantSettingsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
