#pragma once

#include <filesystem>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cloud_tts_pipeline.hpp"
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

enum class RegistryKind {
    Piper,
    Ai,
};

std::filesystem::path UserDataDirectory();
std::filesystem::path DownloadCacheDirectory();
void CreateDirectory(const std::filesystem::path& path);
const char* RegistryFilename(RegistryKind kind);
RegistryLoadResult LoadUserRegistryJson(const char* filename, Json* root);
RegistryLoadResult LoadUserRegistryJson(RegistryKind kind, Json* root);
RegistryDownloadResult DownloadRegistry(const char* url, const char* filename);
std::string JsonLabel(const Json& object, const char* primary, const char* fallback);
std::string BracketLabel(const std::string& label);

} // namespace assistant_modal_detail

class TtsModalContent : public IModalContent {
public:
    TtsModalContent(
        const Theme* theme,
        CloudTtsPipeline* pipeline,
        std::function<void()> prepare_current_file);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "Text-to-Speech"; }
    ModalSizePreference GetModalSizePreference() const override { return {80, 24}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshLibrary();

private:
    void SyncMetadataFieldsFromSelection();
    void SaveSelectedMetadata();
    void ShowSelectedBookInfo();
    void RebuildMetadataOptions();
    void RebuildVoiceOptions();

    const Theme* theme_ = nullptr;
    CloudTtsPipeline* pipeline_ = nullptr;
    std::function<void()> prepare_current_file_;
    std::vector<CloudTtsPipeline::BookInfo> library_books_;
    std::vector<std::string> library_book_labels_ = {"No prepared books"};
    int selected_library_book_ = 0;
    std::string metadata_title_;
    std::string metadata_author_;
    std::string metadata_series_;
    std::string metadata_genre_;
    std::string metadata_series_index_;
    std::vector<std::string> known_series_;
    std::vector<std::string> known_genres_;
    std::vector<std::string> language_codes_ = {"unknown"};
    std::vector<std::string> language_labels_ = {"Unknown"};
    int selected_language_ = 0;
    std::vector<std::string> piper_voice_ids_;
    std::vector<std::string> piper_voice_labels_ = {"No voices"};
    std::vector<bool> piper_voice_installed_;
    int selected_piper_voice_ = 0;
    bool show_selected_book_info_ = false;
    std::string status_ = "TTS library not loaded";
    ftxui::Component refresh_library_button_;
    ftxui::Component prepare_current_file_button_;
    ftxui::Component save_metadata_button_;
    ftxui::Component info_button_;
    ftxui::Component library_menu_;
    ftxui::Component title_input_;
    ftxui::Component author_input_;
    ftxui::Component series_input_;
    ftxui::Component genre_input_;
    ftxui::Component series_index_input_;
    ftxui::Component language_menu_;
    ftxui::Component piper_voice_menu_;
    ftxui::Component controls_;
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
    ModalSizePreference GetModalSizePreference() const override { return {80, 28}; }
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
    void FetchRegistries();
    bool ResolveAiRuntimeDownload();
    void StartAiRuntimeDownload();
    void StartAiRuntimeDelete();
    void ConfirmAiRuntimeDelete();
    void CancelAiRuntimeDelete();
    void StartAiModelDownload();
    void StartAiModelDelete();
    void ConfirmAiModelDelete();
    void CancelAiModelDelete();
    void StartTtsVoiceDownload();
    void StartTtsVoiceDelete();
    void ConfirmTtsVoiceDelete();
    void CancelTtsVoiceDelete();
    void TestTtsVoice();
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
    std::string ai_runtime_download_url_;
    std::string ai_runtime_asset_name_;
    float tts_progress_ = 0.0f;
    std::atomic<float> ai_progress_{0.0f};

    std::thread tts_download_thread_;
    mutable std::mutex tts_download_mutex_;
    std::atomic_bool tts_cancel_download_{false};
    bool tts_downloading_ = false;
    bool tts_download_visible_ = false;
    bool tts_refresh_after_download_ = false;
    bool tts_delete_confirm_visible_ = false;
    std::vector<Json> tts_delete_pending_voices_;
    std::string tts_download_current_file_;
    unsigned long long tts_downloaded_bytes_ = 0;
    unsigned long long tts_total_bytes_ = 0;
    float tts_progress_ratio_ = 0.0f;
    std::thread ai_runtime_thread_;
    mutable std::mutex ai_runtime_mutex_;
    bool ai_runtime_downloading_ = false;
    bool ai_runtime_delete_confirm_visible_ = false;
    bool ai_runtime_progress_visible_ = false;
    bool ai_runtime_extracting_ = false;
    unsigned long long ai_runtime_downloaded_bytes_ = 0;
    unsigned long long ai_runtime_total_bytes_ = 0;
    std::thread ai_model_thread_;
    bool ai_model_downloading_ = false;
    bool ai_model_deleting_ = false;
    bool ai_model_progress_visible_ = false;
    bool ai_model_delete_confirm_visible_ = false;
    bool ai_refresh_after_model_download_ = false;
    std::string ai_model_delete_pending_filename_;
    unsigned long long ai_model_downloaded_bytes_ = 0;
    unsigned long long ai_model_total_bytes_ = 0;

    ftxui::Component tts_tab_button_;
    ftxui::Component ai_tab_button_;
    ftxui::Component tab_buttons_;
    ftxui::Component tts_language_menu_;
    ftxui::Component tts_voice_menu_;
    ftxui::Component ai_model_menu_;
    ftxui::Component fetch_tts_button_;
    ftxui::Component tts_download_button_;
    ftxui::Component tts_delete_button_;
    ftxui::Component tts_confirm_delete_button_;
    ftxui::Component tts_cancel_delete_button_;
    ftxui::Component tts_test_button_;
    ftxui::Component fetch_ai_button_;
    ftxui::Component ai_runtime_download_button_;
    ftxui::Component ai_runtime_delete_button_;
    ftxui::Component ai_runtime_confirm_delete_button_;
    ftxui::Component ai_runtime_cancel_delete_button_;
    ftxui::Component ai_model_download_button_;
    ftxui::Component ai_delete_model_button_;
    ftxui::Component ai_model_confirm_delete_button_;
    ftxui::Component ai_model_cancel_delete_button_;
    ftxui::Component tab_body_container_;
    ftxui::Component container_;
};

class TtsModal {
public:
    TtsModal(
        const Theme* theme,
        CloudTtsPipeline* pipeline,
        std::function<void()> prepare_current_file);

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
