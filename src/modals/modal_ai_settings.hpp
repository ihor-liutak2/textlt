#pragma once

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
    ModalSizePreference GetModalSizePreference() const override { return {108, 31}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override;

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromConfig();

private:
    void LoadModels();
    void ApplyPendingWorkerResult();
    bool SaveConnectionSettings();
    void SelectProvider(AiProvider provider);
    void ConnectAndRefreshServerModels();
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
    void StartWorker(std::function<void()> operation);
    void RequestRedraw() const;
    ftxui::Element RenderContent(const Theme& theme);

    const Theme* theme_ = nullptr;
    EditorConfig* config_ = nullptr;
    std::function<void()> request_redraw_;

    std::string server_url_ = "http://127.0.0.1:11434";
    int server_url_cursor_ = 0;
    AiProvider provider_ = AiProvider::Auto;
    std::string status_ = "Ready";
    std::string detected_provider_ = "Not connected";
    bool busy_ = false;
    bool runtime_delete_confirmation_ = false;
    bool model_delete_confirmation_ = false;
    std::string pending_model_delete_filename_;

    std::vector<AiModelInfo> models_;
    std::vector<AiModelInfo> server_models_;
    std::vector<std::string> model_labels_{"No models"};
    std::vector<std::string> model_descriptions_;
    int selected_model_ = 0;
    int persisted_model_index_ = -1;

    mutable std::mutex state_mutex_;
    std::thread worker_;
    bool pending_reload_ = false;
    bool pending_server_models_ = false;
    bool pending_connection_success_ = false;
    AiProvider pending_detected_provider_ = AiProvider::Auto;
    std::string pending_status_;
    std::vector<AiModelInfo> pending_models_;

    ftxui::Component server_url_input_;
    ftxui::Component provider_auto_button_;
    ftxui::Component provider_ollama_button_;
    ftxui::Component provider_openai_button_;
    ftxui::Component provider_local_button_;
    ftxui::Component save_connection_button_;
    ftxui::Component connect_button_;
    ftxui::Component fetch_registry_button_;
    ftxui::Component runtime_download_button_;
    ftxui::Component runtime_delete_button_;
    ftxui::Component runtime_confirm_delete_button_;
    ftxui::Component runtime_cancel_delete_button_;
    ftxui::Component model_download_button_;
    ftxui::Component model_delete_button_;
    ftxui::Component model_confirm_delete_button_;
    ftxui::Component model_cancel_delete_button_;
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
