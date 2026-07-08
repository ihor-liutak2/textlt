#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "json_utils.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class AiSettingsModalContent : public IModalContent {
public:
    explicit AiSettingsModalContent(
        const Theme* theme,
        std::function<void()> request_redraw = {});
    ~AiSettingsModalContent() override;

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "AI Settings"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {100, 26}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override;

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    void LoadAiRegistry();
    void FetchAiRegistry();
    bool ResolveAiRuntimeDownload();
    void StartAiRuntimeDownload();
    void StartAiRuntimeDelete();
    void ConfirmAiRuntimeDelete();
    void CancelAiRuntimeDelete();
    void StartAiModelDownload();
    void StartAiModelDelete();
    void ConfirmAiModelDelete();
    void CancelAiModelDelete();
    void RequestRedraw() const;
    ftxui::Element RenderAiSettings(const Theme& theme);

    const Theme* theme_ = nullptr;
    std::function<void()> request_redraw_;

    std::string ai_status_ = "Registry not loaded";
    std::vector<std::string> ai_model_labels_ = {"No models"};
    int selected_ai_model_ = 0;
    std::string ai_runtime_download_url_;
    std::string ai_runtime_asset_name_;
    std::atomic<float> ai_progress_{0.0f};

    std::thread ai_runtime_thread_;
    mutable std::mutex ai_runtime_mutex_;
    bool ai_runtime_downloading_ = false;
    bool ai_runtime_delete_confirm_visible_ = false;
    bool ai_runtime_progress_visible_ = false;
    bool ai_runtime_extracting_ = false;
    std::thread ai_model_thread_;
    std::thread fetch_thread_;
    bool ai_model_downloading_ = false;
    bool ai_model_deleting_ = false;
    bool ai_model_progress_visible_ = false;
    bool ai_model_delete_confirm_visible_ = false;
    bool ai_refresh_after_model_download_ = false;
    std::string ai_model_delete_pending_filename_;

    ftxui::Component fetch_ai_button_;
    ftxui::Component ai_runtime_download_button_;
    ftxui::Component ai_runtime_delete_button_;
    ftxui::Component ai_runtime_confirm_delete_button_;
    ftxui::Component ai_runtime_cancel_delete_button_;
    ftxui::Component ai_model_download_button_;
    ftxui::Component ai_delete_model_button_;
    ftxui::Component ai_model_confirm_delete_button_;
    ftxui::Component ai_model_cancel_delete_button_;
    ftxui::Component ai_model_menu_;
    ftxui::Component container_;
};

class AiSettingsModal {
public:
    explicit AiSettingsModal(
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
    std::shared_ptr<AiSettingsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
