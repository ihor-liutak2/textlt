#pragma once

#include <filesystem>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "curl_manager.hpp"
#include "json_utils.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "piper_manager.hpp"
#include "remote/remote_http_client.hpp"
#include "theme.hpp"

namespace textlt {

namespace assistant_modal_detail {

constexpr const char* kPiperRegistryFile = "piper_voices_index.json";
constexpr const char* kAiRegistryFile = "ollama_models_index.json";

enum class RegistryDownloadResult {
    Saved,
    Failed,
    Empty,
    InvalidJson,
};

enum class RegistryLoadResult {
    Loaded,
    Missing,
    ParseFailed,
};

enum class RegistryKind {
    Piper,
    Ai,
};

std::filesystem::path UserDataDirectory();
std::filesystem::path DownloadCacheDirectory();
void EnsureDirectory(const std::filesystem::path& path);
const char* RegistryFilename(RegistryKind kind);
RegistryLoadResult LoadUserRegistryJson(const char* filename, Json* root);
RegistryLoadResult LoadUserRegistryJson(RegistryKind kind, Json* root);
RegistryDownloadResult DownloadRegistry(const char* url, const char* filename);
std::string JsonLabel(const Json& object, const char* primary, const char* fallback);
std::string BracketLabel(const std::string& label);

} // namespace assistant_modal_detail

class AssistantSettingsModalContent : public IModalContent {
public:
    explicit AssistantSettingsModalContent(
        const Theme* theme,
        std::function<void()> request_redraw = {},
        std::function<void()> on_close = {});
    ~AssistantSettingsModalContent() override;

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "TTS Settings"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {100, 34}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override;
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override { return 3; }
    ftxui::Element RenderCustomFooter() override;

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void CloseTtsTestPopup();

private:
    void LoadRegistries();
    void LoadPiperRegistry();
    void RebuildTtsVoices();
    void FetchRegistries();
    void StartPiperRuntimeInstall();
    void StartTtsVoiceDownload();
    void StartTtsVoiceDelete();
    void ConfirmTtsVoiceDelete();
    void CancelTtsVoiceDelete();
    void TestTtsVoice();
    void RefreshPiperServerStatus();
    void StartPiperServer();
    void StopPiperServer();
    void ReloadPiperServer();
    void CheckPiperServerHealth();
    void ApplyPiperServerStatusResponse(const RemoteHttpResponse& response,
                                        int port,
                                        const std::string& offline_status);
    bool ReadPiperServerSettings(int* port,
                                 PiperRunOptions* options,
                                 std::string* error) const;
    bool GeneratePiperTestAudio(const Json& voice,
                                const std::string& text,
                                const std::filesystem::path& output_wav,
                                std::string* error);
    void ShowTtsTestPopup(std::string text);
    void ApplyTtsDownloadCompletion();
    void RequestRedraw() const;
    std::string CurrentFooterMessage() const;
    ftxui::Element RenderHeaderRow(const Theme& theme);
    ftxui::Element RenderTtsTab(const Theme& theme);
    ftxui::Element RenderTtsTestPopup(const Theme& theme) const;
    ftxui::Element RenderPiperServerTab(const Theme& theme);

    const Theme* theme_ = nullptr;
    std::function<void()> request_redraw_;
    std::function<void()> on_close_;
    int selected_tab_ = 0;

    std::string tts_status_ = "Registry not loaded";
    std::vector<std::string> tts_language_labels_ = {"No languages"};
    std::vector<std::string> tts_voice_labels_ = {"No voices"};
    int selected_tts_language_ = 0;
    int selected_tts_voice_ = 0;

    std::string piper_server_status_ = "Press Start server or Check status";
    std::string piper_server_running_ = "Stopped";
    std::string piper_server_uptime_ = "-";
    std::string piper_server_pid_ = "-";
    std::string piper_server_requests_ = "-";
    std::string piper_server_voice_state_ = "Not loaded";
    std::string piper_server_active_voice_ = "-";
    std::string piper_server_port_;
    std::string piper_server_noise_scale_ = "0.667";
    std::string piper_server_sentence_silence_ = "0.15";
    std::string piper_server_speaker_id_ = "0";
    bool piper_server_use_cuda_ = false;
    float tts_progress_ = 0.0f;

    std::thread tts_download_thread_;
    std::thread tts_runtime_thread_;
    mutable std::mutex tts_download_mutex_;
    std::atomic_bool tts_cancel_download_{false};
    bool tts_downloading_ = false;
    bool tts_download_visible_ = false;
    bool tts_refresh_after_download_ = false;
    bool tts_delete_confirm_visible_ = false;
    bool tts_test_popup_visible_ = false;
    std::string tts_test_popup_text_;
    std::vector<Json> tts_delete_pending_voices_;
    std::string tts_download_current_file_;
    float tts_progress_ratio_ = 0.0f;
    int popup_layer_index_ = 0;
    std::thread fetch_thread_;

    ftxui::Component tts_tab_button_;
    ftxui::Component piper_server_tab_button_;
    ftxui::Component tab_buttons_;
    ftxui::Component tts_language_menu_;
    ftxui::Component tts_voice_menu_;
    ftxui::Component piper_server_port_input_;
    ftxui::Component piper_server_noise_scale_input_;
    ftxui::Component piper_server_sentence_silence_input_;
    ftxui::Component piper_server_speaker_id_input_;
    ftxui::Component piper_server_cuda_checkbox_;
    ftxui::Component fetch_tts_button_;
    ftxui::Component tts_runtime_install_button_;
    ftxui::Component tts_download_button_;
    ftxui::Component tts_delete_button_;
    ftxui::Component tts_confirm_delete_button_;
    ftxui::Component tts_cancel_delete_button_;
    ftxui::Component tts_test_button_;
    ftxui::Component tts_test_popup_close_button_;
    ftxui::Component piper_server_refresh_button_;
    ftxui::Component piper_server_start_button_;
    ftxui::Component piper_server_reload_button_;
    ftxui::Component piper_server_health_button_;
    ftxui::Component piper_server_shutdown_button_;
    ftxui::Component footer_close_button_;
    ftxui::Component tab_body_container_;
    ftxui::Component container_;
};

class AssistantSettingsModal {
public:
    explicit AssistantSettingsModal(
        const Theme* theme,
        std::function<void()> request_redraw = {});

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<AssistantSettingsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
