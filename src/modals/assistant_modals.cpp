#include "assistant_modals.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "assistant_download_progress.hpp"
#include "piper_manager.hpp"
#include "ui_button.hpp"

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

void EnsureDirectory(const std::filesystem::path& path) {
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

const char* RegistryFilename(RegistryKind kind) {
    return kind == RegistryKind::Piper ? kPiperRegistryFile : kAiRegistryFile;
}

RegistryLoadResult LoadUserRegistryJson(RegistryKind kind, Json* root) {
    return LoadUserRegistryJson(RegistryFilename(kind), root);
}

std::string JsonLabel(const Json& object, const char* primary, const char* fallback) {
    const std::string value = JsonString(object, primary);
    return value.empty() ? JsonString(object, fallback) : value;
}

std::string BracketLabel(const std::string& label) {
    return !label.empty() && label.front() == '[' ? label : "[" + label + "]";
}

namespace {

std::string EscapeRawNewlinesInJsonStrings(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    bool in_string = false;
    bool escaped = false;
    for (char character : input) {
        if (escaped) {
            output.push_back(character);
            escaped = false;
            continue;
        }
        if (character == '\\' && in_string) {
            output.push_back(character);
            escaped = true;
            continue;
        }
        if (character == '"') {
            in_string = !in_string;
            output.push_back(character);
            continue;
        }
        if (in_string && character == '\n') {
            output += "\\n";
            continue;
        }
        if (in_string && character == '\r') {
            continue;
        }
        output.push_back(character);
    }
    return output;
}


} // namespace

RegistryDownloadResult DownloadRegistry(
    const char* url,
    const char* filename,
    const std::atomic<bool>* cancel_requested,
    RemoteCommandControl* command_control) {
    const std::filesystem::path registry_directory = RegistryDirectory();
    if (registry_directory.empty()) {
        return RegistryDownloadResult::Failed;
    }

    EnsureDirectory(registry_directory);

    const std::filesystem::path final_path = registry_directory / filename;
    CurlManager::RequestOptions options;
    options.no_cache = true;
    options.fresh_connect = true;
    const CurlManager::Response response =
        CurlManager::Get(
            CurlManager::WithCacheBust(url), options, cancel_requested, command_control);
    if (!response.ok) {
        return RegistryDownloadResult::Failed;
    }

    if (response.body.empty()) {
        return RegistryDownloadResult::Empty;
    }

    std::string body = response.body;
    Json parsed = Json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        body = EscapeRawNewlinesInJsonStrings(body);
        parsed = Json::parse(body, nullptr, false);
    }
    if (parsed.is_discarded() || !parsed.is_object()) {
        return RegistryDownloadResult::InvalidJson;
    }

    if (!WriteJsonAtomically(final_path, parsed)) {
        return RegistryDownloadResult::Failed;
    }

    return RegistryDownloadResult::Saved;
}

} // namespace assistant_modal_detail

AssistantSettingsModalContent::AssistantSettingsModalContent(
    const Theme* theme,
    std::function<void()> request_redraw,
    std::function<void()> on_close)
    : theme_(theme),
      request_redraw_(std::move(request_redraw)),
      on_close_(std::move(on_close)) {
    auto make_button = [this](std::string label,
                              std::function<void()> on_click,
                              ButtonRole role = ButtonRole::Default) {
        ButtonSpec spec = ButtonSpecFromLabel(std::move(label), role, ButtonVariant::Minimal);
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = ButtonCaptionText(spec);
        option.on_click = std::move(on_click);
        option.transform = [this, spec = std::move(spec)](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            return RenderModalFlatButton(theme, spec, state.focused || state.active);
        };
        return ftxui::Button(option);
    };
    auto make_tab_button = [this](std::string label, int tab_index) {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = "  " + label + "  ";
        option.on_click = [this, tab_index] { selected_tab_ = tab_index; };
        option.transform = [this, tab_index, label = std::move(label)](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            return RenderModalTabButton(
                theme,
                label,
                selected_tab_ == tab_index,
                state.focused || state.active);
        };
        return ftxui::Button(option);
    };

    tts_tab_button_ = make_tab_button("TTS", 0);
    piper_server_tab_button_ = make_tab_button("Server", 1);
    tab_buttons_ = ftxui::Container::Horizontal({
        tts_tab_button_,
        piper_server_tab_button_,
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

    ftxui::InputOption server_input_option;
    server_input_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element input = state.element |
            ftxui::color(theme.modal_input_fg);
        if (state.focused) {
            input = input | ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return input;
    };
    piper_server_port_input_ = ftxui::Input(
        &piper_server_port_, "59123", server_input_option);
    piper_server_noise_scale_input_ = ftxui::Input(
        &piper_server_noise_scale_, "0.667", server_input_option);
    piper_server_sentence_silence_input_ = ftxui::Input(
        &piper_server_sentence_silence_, "0.15", server_input_option);
    if (piper_server_speaker_id_.empty()) {
        piper_server_speaker_id_ = "0";
    }
    piper_server_speaker_id_input_ = ftxui::Input(
        &piper_server_speaker_id_, "0", server_input_option);
    ftxui::CheckboxOption server_checkbox_option = ftxui::CheckboxOption::Simple();
    server_checkbox_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(
            std::string(state.state ? "[X] " : "[ ] ") + state.label);
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return item | ftxui::color(theme.modal_text_color);
    };
    piper_server_cuda_checkbox_ = ftxui::Checkbox(
        "Use CUDA", &piper_server_use_cuda_, server_checkbox_option);

    fetch_tts_button_ = make_button("Fetch registry", [this] { FetchRegistries(); }, ButtonRole::Primary);
    tts_runtime_install_button_ =
        make_button("Install Piper", [this] { StartPiperRuntimeInstall(); });
    tts_download_button_ = make_button("Download voice", [this] { StartTtsVoiceDownload(); });
    tts_delete_button_ = make_button("Delete", [this] { StartTtsVoiceDelete(); });
    tts_confirm_delete_button_ =
        make_button("Confirm delete", [this] { ConfirmTtsVoiceDelete(); });
    tts_cancel_delete_button_ =
        make_button("Cancel", [this] { CancelTtsVoiceDelete(); });
    tts_test_button_ = make_button("Test", [this] { TestTtsVoice(); }, ButtonRole::Primary);
    tts_test_popup_close_button_ =
        make_button("Close", [this] { CloseTtsTestPopup(); });
    piper_server_refresh_button_ =
        make_button("Refresh", [this] { RefreshPiperServerStatus(); }, ButtonRole::Primary);
    piper_server_start_button_ =
        make_button("Start server", [this] { StartPiperServer(); }, ButtonRole::Primary);
    piper_server_reload_button_ =
        make_button("Reload server", [this] { ReloadPiperServer(); }, ButtonRole::Primary);
    piper_server_health_button_ =
        make_button("Check status", [this] { CheckPiperServerHealth(); }, ButtonRole::Primary);
    piper_server_shutdown_button_ =
        make_button("Stop server", [this] { StopPiperServer(); }, ButtonRole::Danger);
    footer_close_button_ = make_button("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    }, ButtonRole::Cancel);

    tab_body_container_ = ftxui::Container::Tab({
        ftxui::Container::Vertical({
            tts_language_menu_,
            tts_voice_menu_,
            ftxui::Container::Horizontal({
                tts_confirm_delete_button_,
                tts_cancel_delete_button_,
            }),
        }),
        ftxui::Container::Vertical({
            piper_server_port_input_,
            piper_server_cuda_checkbox_,
            piper_server_noise_scale_input_,
            piper_server_sentence_silence_input_,
            piper_server_speaker_id_input_,
        }),
    }, &selected_tab_);

    auto primary_controls = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
        fetch_tts_button_,
        tts_runtime_install_button_,
        tts_download_button_,
        tts_delete_button_,
        tts_test_button_,
        piper_server_refresh_button_,
        piper_server_start_button_,
        piper_server_reload_button_,
        piper_server_health_button_,
        piper_server_shutdown_button_,
        footer_close_button_,
    });
    auto popup_controls = ftxui::CatchEvent(tts_test_popup_close_button_, [this](ftxui::Event event) {
        if (event == ftxui::Event::Escape) {
            CloseTtsTestPopup();
            return true;
        }
        return false;
    });
    container_ = ftxui::Container::Tab({primary_controls, popup_controls}, &popup_layer_index_);

    LoadRegistries();
}

AssistantSettingsModalContent::~AssistantSettingsModalContent() {
    tts_cancel_download_ = true;
    if (tts_download_thread_.joinable()) {
        tts_download_thread_.join();
    }
    if (tts_runtime_thread_.joinable()) {
        tts_runtime_thread_.join();
    }
    if (fetch_thread_.joinable()) {
        fetch_thread_.join();
    }
}

ftxui::Element AssistantSettingsModalContent::Render() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ftxui::Element body;
    if (selected_tab_ == 0) {
        body = RenderTtsTab(theme);
    } else {
        body = RenderPiperServerTab(theme);
    }
    return ftxui::vbox({
        RenderHeaderRow(theme),
        ftxui::separator() | ftxui::color(theme.modal_border),
        body | ftxui::flex,
    }) |
        ftxui::bgcolor(theme.modal_input_bg) |
        ftxui::color(theme.modal_input_fg);
}

ftxui::Element AssistantSettingsModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}

ftxui::Element AssistantSettingsModalContent::RenderHeaderRow(const Theme& theme) {
    using namespace ftxui;
    return hbox({
        tts_tab_button_->Render(),
        text(" "),
        piper_server_tab_button_->Render(),
        filler(),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

std::string AssistantSettingsModalContent::GetFooterText() const {
    return CurrentFooterMessage();
}

std::string AssistantSettingsModalContent::CurrentFooterMessage() const {
    auto trim_footer_text = [](const std::string& text) {
        constexpr size_t kMaxFooterTextLength = 88;
        if (text.size() <= kMaxFooterTextLength) {
            return text;
        }
        return text.substr(0, kMaxFooterTextLength - 1) + "…";
    };

    if (selected_tab_ == 0) {
        std::lock_guard<std::mutex> lock(tts_download_mutex_);
        return trim_footer_text(tts_status_);
    }
    return trim_footer_text(piper_server_status_);
}

ftxui::Element AssistantSettingsModalContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements buttons;
    auto append_button = [&buttons](const ftxui::Component& button) {
        if (!button) {
            return;
        }
        if (!buttons.empty()) {
            buttons.push_back(text(" "));
        }
        buttons.push_back(button->Render());
    };

    if (selected_tab_ == 0) {
        append_button(fetch_tts_button_);
        append_button(tts_runtime_install_button_);
        append_button(tts_download_button_);
        append_button(tts_delete_button_);
        append_button(tts_test_button_);
    } else {
        append_button(piper_server_refresh_button_);
        append_button(piper_server_start_button_);
        append_button(piper_server_reload_button_);
        append_button(piper_server_health_button_);
        append_button(piper_server_shutdown_button_);
    }

    return vbox({
        hbox({
            text(" " + CurrentFooterMessage()) | dim | color(theme.modal_text_color),
            filler(),
        }),
        separator() | color(theme.modal_border),
        hbox({
            hbox(std::move(buttons)),
            filler(),
            footer_close_button_ ? footer_close_button_->Render() : text(""),
        }),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

void AssistantSettingsModalContent::LoadRegistries() {
    LoadPiperRegistry();
}

void AssistantSettingsModalContent::FetchRegistries() {
    using namespace assistant_modal_detail;

    {
        std::lock_guard<std::mutex> tts_lock(tts_download_mutex_);
        if (tts_downloading_) {
            tts_status_ = "Voice download is running";
            return;
        }
    }
    if (fetch_thread_.joinable()) {
        fetch_thread_.join();
    }

    {
        std::lock_guard<std::mutex> tts_lock(tts_download_mutex_);
        tts_status_ = "Fetching TTS registry...";
        tts_progress_ = 0.0f;
    }
    RequestRedraw();

    fetch_thread_ = std::thread([this] {
        const RegistryDownloadResult piper_download_result =
            DownloadRegistry(CurlManager::kPiperRegistryUrl, RegistryFilename(RegistryKind::Piper));
        LoadPiperRegistry();

        auto registry_status = [](const std::string& label,
                                  RegistryDownloadResult download_result,
                                  RegistryLoadResult load_result) {
            if (download_result == RegistryDownloadResult::Saved &&
                load_result == RegistryLoadResult::Loaded) {
                return label + " registry loaded";
            }
            if (download_result == RegistryDownloadResult::Empty) {
                return label + " registry file is empty";
            }
            if (download_result == RegistryDownloadResult::InvalidJson) {
                return "Downloaded " + label + " registry is invalid JSON";
            }
            if (load_result == RegistryLoadResult::Missing) {
                return label + " registry not loaded";
            }
            if (load_result == RegistryLoadResult::ParseFailed) {
                return label + " registry parse failed";
            }
            return label + " registry download failed";
        };

        Json piper_root;
        const RegistryLoadResult piper_load_result =
            LoadUserRegistryJson(RegistryKind::Piper, &piper_root);

        {
            std::lock_guard<std::mutex> lock(tts_download_mutex_);
            tts_status_ = registry_status("TTS", piper_download_result, piper_load_result);
        }
        RequestRedraw();
    });
}

AssistantSettingsModal::AssistantSettingsModal(
    const Theme* theme,
    std::function<void()> request_redraw)
    : theme_(theme) {
    content_ = std::make_shared<AssistantSettingsModalContent>(
        theme_,
        std::move(request_redraw),
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AssistantSettingsModal::View() const {
    return modal_;
}

void AssistantSettingsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->CloseTtsTestPopup();
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
