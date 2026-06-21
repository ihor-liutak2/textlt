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
#include "json_utils.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

namespace assistant_modal_detail {

constexpr const char* kPiperRegistryUrl =
    "https://ihor-liutak2.github.io/textlt/registries/piper_voices_index.json";
constexpr const char* kAiRegistryUrl =
    "https://ihor-liutak2.github.io/textlt/registries/ollama_models_index.json";
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

std::filesystem::path UserDataDirectory();
std::filesystem::path DownloadCacheDirectory();
void CreateDirectory(const std::filesystem::path& path);
RegistryLoadResult LoadUserRegistryJson(const char* filename, Json* root);
RegistryDownloadResult DownloadRegistry(const char* url, const char* filename);
std::string JsonLabel(const Json& object, const char* primary, const char* fallback);
std::string BracketLabel(const std::string& label);

} // namespace assistant_modal_detail

class TtsModalContent : public IModalContent {
public:
    explicit TtsModalContent(const Theme* theme);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "Text-to-Speech"; }
    ModalSizePreference GetModalSizePreference() const override { return {58, 14}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    const Theme* theme_ = nullptr;
    ftxui::Component renderer_;
};

class AiActionsModalContent : public IModalContent {
public:
    explicit AiActionsModalContent(const Theme* theme);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "AI Actions"; }
    ModalSizePreference GetModalSizePreference() const override { return {58, 14}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    const Theme* theme_ = nullptr;
    ftxui::Component renderer_;
};

class AssistantSettingsModalContent : public IModalContent {
public:
    explicit AssistantSettingsModalContent(
        const Theme* theme,
        std::function<void()> request_redraw = {});
    ~AssistantSettingsModalContent() override;

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Assistant Settings"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {74, 22}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override;

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    void LoadRegistries();
    void LoadPiperRegistry();
    void LoadAiRegistry();
    void RebuildTtsVoices();
    void FetchTtsRegistry();
    void FetchAiRegistry();
    void StartTtsVoiceDownload();
    void ApplyTtsDownloadCompletion();
    void RequestRedraw() const;
    void SetTodoStatus(std::string action);
    ftxui::Element RenderTtsTab(const Theme& theme);
    ftxui::Element RenderAiTab(const Theme& theme);

    const Theme* theme_ = nullptr;
    std::function<void()> request_redraw_;
    int selected_tab_ = 0;

    std::string tts_status_ = "Registry not loaded";
    std::vector<std::string> tts_language_labels_ = {"No languages"};
    std::vector<std::string> tts_voice_labels_ = {"No voices"};
    int selected_tts_language_ = 0;
    int selected_tts_voice_ = 0;

    std::string ai_status_ = "Registry not loaded";
    std::vector<std::string> ai_model_labels_ = {"No models"};
    int selected_ai_model_ = 0;
    float tts_progress_ = 0.0f;
    float ai_progress_ = 0.0f;

    std::thread tts_download_thread_;
    mutable std::mutex tts_download_mutex_;
    std::atomic_bool tts_cancel_download_{false};
    bool tts_downloading_ = false;
    bool tts_download_visible_ = false;
    bool tts_refresh_after_download_ = false;
    std::string tts_download_current_file_;
    unsigned long long tts_downloaded_bytes_ = 0;
    unsigned long long tts_total_bytes_ = 0;
    float tts_progress_ratio_ = 0.0f;

    ftxui::Component tts_tab_button_;
    ftxui::Component ai_tab_button_;
    ftxui::Component tab_buttons_;
    ftxui::Component tts_language_menu_;
    ftxui::Component tts_voice_menu_;
    ftxui::Component ai_model_menu_;
    ftxui::Component fetch_tts_button_;
    ftxui::Component tts_download_button_;
    ftxui::Component tts_delete_button_;
    ftxui::Component tts_test_button_;
    ftxui::Component fetch_ai_button_;
    ftxui::Component ai_runtime_download_button_;
    ftxui::Component ai_model_download_button_;
    ftxui::Component ai_delete_model_button_;
    ftxui::Component tab_body_container_;
    ftxui::Component container_;
};

class TtsModal {
public:
    explicit TtsModal(const Theme* theme);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<TtsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

class AiActionsModal {
public:
    explicit AiActionsModal(const Theme* theme);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<AiActionsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
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
