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
    ModalSizePreference GetModalSizePreference() const override { return {100, 28}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromConfig();
    bool SaveConnectionSettings();
    void ConnectAndRefreshServerModels();
    void CancelCurrentTask();
    void PrepareClose();
    bool CanCancelTask() const;

private:
    enum class OperationState { Idle, Starting, Running, Stopping };

    void LoadModels();
    void ApplyPendingWorkerResult();
    void SelectProvider(AiProvider provider);
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
    bool test_popup_visible_ = false;
    bool test_running_ = false;
    int active_panel_ = 0;
    int test_progress_frame_ = 0;
    int operation_progress_frame_ = 0;
    std::string test_result_ = "Press Test to run the selected model.";
    const std::string test_source_ =
        "Corwin wakes from a coma in a New York hospital with amnesia and soon discovers "
        "that he belongs to a superhuman royal family that travels among parallel worlds.";

    std::vector<AiModelInfo> models_;
    std::vector<std::string> model_labels_{"No models"};
    std::vector<std::string> model_descriptions_;
    int selected_model_ = 0;
    int persisted_model_index_ = -1;

    mutable std::mutex state_mutex_;
    std::thread worker_;
    std::atomic<bool> cancel_requested_{false};
    RemoteCommandControl command_control_;
    OperationState operation_state_ = OperationState::Idle;
    bool pending_models_ready_ = false;
    bool pending_connection_success_ = false;
    AiProvider pending_detected_provider_ = AiProvider::Ollama;
    std::string pending_status_;
    std::vector<AiModelInfo> pending_models_;

    ftxui::Component server_url_input_;
    ftxui::Component provider_ollama_button_;
    ftxui::Component provider_openai_button_;
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
