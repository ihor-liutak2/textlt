#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ai/ai_backend.hpp"
#include "editor_config.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "remote/remote_command_runner.hpp"
#include "theme.hpp"

namespace textlt {

class AiSettingsModalContent : public IModalContent {
public:
    AiSettingsModalContent(
        const Theme* theme,
        EditorConfig* config,
        std::function<void()> request_redraw = {});
    ~AiSettingsModalContent() override;

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "AI Settings"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {108, 34}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromConfig();
    bool SaveConnectionSettings();
    void ConnectAndRefreshServerModels();
    void StopCurrentOperation();
    void PrepareClose();
    bool CanStop() const;

private:
    void LoadModels();
    void ApplyPendingWorkerResult();
    void SelectProvider(AiProvider provider);
    void FetchAiRegistry();
    void DownloadRuntime();
    void RequestRuntimeDelete();
    void ConfirmRuntimeDelete();
    void CancelRuntimeDelete();
    void DownloadSelectedModel();
    void RequestSelectedModelDelete();
    void ConfirmSelectedModelDelete();
    void CancelSelectedModelDelete();
    void PersistSelectedModel();
    void StartTest();
    void CloseTestPopup();
    void StartWorker(std::function<void()> operation);
    void RequestRedraw() const;
    ftxui::Element RenderContent(const Theme& theme);
    ftxui::Element RenderTestPopup(const Theme& theme);

    const Theme* theme_ = nullptr;
    EditorConfig* config_ = nullptr;
    std::function<void()> request_redraw_;

    std::string server_url_ = "http://127.0.0.1:11434";
    int server_url_cursor_ = 0;
    AiProvider provider_ = AiProvider::Ollama;
    std::string status_ = "Ready";
    std::string connection_status_ = "Not connected";
    bool busy_ = false;
    bool runtime_delete_confirmation_ = false;
    bool model_delete_confirmation_ = false;
    std::string pending_model_delete_filename_;

    bool test_popup_visible_ = false;
    int active_panel_ = 0;
    bool test_running_ = false;
    int test_progress_frame_ = 0;
    int operation_progress_frame_ = 0;
    std::string test_result_ = "Press Test to run the selected model.";
    const std::string test_source_ =
        "Corwin wake up from a coma in a hospital in New York with amnesia, and he soon discover "
        "that he is part of a superhuman royal family who can wanders among infinite parallel "
        "worlds, called shadows, and rules over the one true world, Amber.";

    std::vector<AiModelInfo> models_;
    std::vector<AiModelInfo> server_models_;
    std::vector<std::string> model_labels_{"No models"};
    std::vector<std::string> model_descriptions_;
    int selected_model_ = 0;
    int persisted_model_index_ = -1;

    enum class OperationState { Idle, Starting, Running, Stopping };

    mutable std::mutex state_mutex_;
    std::thread worker_;
    std::atomic<bool> cancel_requested_{false};
    RemoteCommandControl command_control_;
    OperationState operation_state_ = OperationState::Idle;
    bool pending_reload_ = false;
    bool pending_server_models_ = false;
    bool pending_connection_success_ = false;
    AiProvider pending_detected_provider_ = AiProvider::Ollama;
    std::string pending_status_;
    std::vector<AiModelInfo> pending_models_;

    ftxui::Component server_url_input_;
    ftxui::Component provider_ollama_button_;
    ftxui::Component provider_openai_button_;
    ftxui::Component provider_local_button_;
    ftxui::Component fetch_registry_button_;
    ftxui::Component runtime_download_button_;
    ftxui::Component runtime_delete_button_;
    ftxui::Component runtime_confirm_delete_button_;
    ftxui::Component runtime_cancel_delete_button_;
    ftxui::Component model_download_button_;
    ftxui::Component model_delete_button_;
    ftxui::Component model_confirm_delete_button_;
    ftxui::Component model_cancel_delete_button_;
    ftxui::Component test_button_;
    ftxui::Component close_test_button_;
    ftxui::Component model_menu_;
    ftxui::Component container_;
};

class AiSettingsModal {
public:
    AiSettingsModal(
        const Theme* theme,
        EditorConfig* config,
        std::function<void()> request_redraw = {});

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<AiSettingsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
