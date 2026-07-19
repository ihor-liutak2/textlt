#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ai/ai_backend.hpp"
#include "ai/ai_quick_status.hpp"
#include "editor/document_session.hpp"
#include "editor_config.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "remote/remote_command_runner.hpp"
#include "theme.hpp"

namespace textlt {

struct AiDocumentTarget {
    std::shared_ptr<DocumentSession> session;
    DocumentTransformTarget range;
};

using CaptureAiTargetCallback =
    std::function<bool(bool whole_document, AiDocumentTarget& target, std::string& error)>;
using ApplyAiTargetCallback =
    std::function<bool(const AiDocumentTarget& target, const std::string& text, std::string& error)>;

class AiActionsModalContent : public IModalContent {
public:
    AiActionsModalContent(
        const Theme* theme,
        EditorConfig* config,
        CaptureAiTargetCallback capture_target,
        ApplyAiTargetCallback apply_target,
        std::function<void()> request_redraw = {},
        std::function<void(const std::string&)> notify_status = {},
        std::function<void(bool, const std::string&)> quick_completion = {});
    ~AiActionsModalContent() override;

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "AI Actions"; }
    ModalSizePreference GetModalSizePreference() const override { return {90, 31}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromConfig();
    bool HandleShortcut(ftxui::Event event);
    bool StartQuickAction(AiActionType action, std::string& error);
    void PrepareQuickActions();
    AiQuickStatusSnapshot QuickStatus() const;
    void StopQuickAction();
    void Poll();
    void PrepareClose();
    void ShowInfo();
    void Stop();
    bool CanStop() const;

private:
    void SetEditStyle(AiEditStyle style);
    bool StartAction(
        AiActionType action,
        bool force_current_paragraph,
        std::string* error_out = nullptr);
    void ApplyPendingResult();
    void StartQuickReadinessCheck();
    void StopQuickReadinessCheck();
    bool PersistOptions();
    bool ValidateLanguages(std::string& error, bool require_distinct);
    void FilterLanguages(bool source);
    void CommitLanguageSelection(bool source);
    void FinalizeLanguageInput(bool source);
    void FinalizeUnfocusedLanguageInputs();
    void CloseInfo();
    void RequestRedraw() const;
    std::string SelectedModelLabel() const;
    std::string SelectedBackendLabel() const;
    ftxui::Element RenderContent(const Theme& theme);
    ftxui::Element RenderLanguageSuggestions(const Theme& theme);
    ftxui::Element RenderInfoPopup(const Theme& theme);

    const Theme* theme_ = nullptr;
    EditorConfig* config_ = nullptr;
    CaptureAiTargetCallback capture_target_;
    ApplyAiTargetCallback apply_target_;
    std::function<void()> request_redraw_;
    std::function<void(const std::string&)> notify_status_;
    std::function<void(bool, const std::string&)> quick_completion_;

    std::vector<std::string> languages_;
    std::vector<std::string> filtered_source_languages_;
    std::vector<std::string> filtered_target_languages_;
    std::string source_language_ = "English";
    std::string target_language_ = "Ukrainian";
    int source_language_cursor_ = 0;
    int target_language_cursor_ = 0;
    int selected_source_language_ = 0;
    int selected_target_language_ = 0;
    AiEditStyle edit_style_ = AiEditStyle::Conversational;
    bool whole_document_ = false;
    bool persisted_whole_document_ = false;
    std::string persisted_source_language_;
    std::string persisted_target_language_;
    std::string committed_source_language_ = "English";
    std::string committed_target_language_ = "Ukrainian";
    AiEditStyle persisted_edit_style_ = AiEditStyle::Conversational;
    bool info_visible_ = false;
    int active_panel_ = 0;
    bool source_language_was_focused_ = false;
    bool target_language_was_focused_ = false;

    enum class OperationState { Idle, Starting, Generating, Stopping };

    mutable std::mutex state_mutex_;
    std::thread worker_;
    std::atomic<bool> cancel_requested_{false};
    RemoteCommandControl command_control_;
    OperationState operation_state_ = OperationState::Idle;
    bool busy_ = false;
    int progress_frame_ = 0;
    std::string status_ = "Ready";
    std::string progress_text_ = "No AI output yet.";
    bool pending_result_ = false;
    bool discard_result_ = false;
    AiBackendResult pending_backend_result_;
    AiDocumentTarget pending_target_;
    AiActionType pending_action_ = AiActionType::Translate;
    bool pending_quick_action_ = false;
    AiActionType active_action_ = AiActionType::Translate;

    std::thread quick_readiness_worker_;
    std::atomic<bool> quick_readiness_cancel_{false};
    RemoteCommandControl quick_readiness_control_;
    bool quick_readiness_checking_ = false;
    bool quick_ready_ = false;
    std::string quick_readiness_status_ = "Not checked";

    ftxui::Component source_language_input_;
    ftxui::Component target_language_input_;
    ftxui::Component source_language_menu_;
    ftxui::Component target_language_menu_;
    ftxui::Component conversational_button_;
    ftxui::Component business_button_;
    ftxui::Component whole_document_checkbox_;
    ftxui::Component translate_button_;
    ftxui::Component edit_button_;
    ftxui::Component close_info_button_;
    ftxui::Component container_;
};

class AiActionsModal {
public:
    AiActionsModal(
        const Theme* theme,
        EditorConfig* config,
        CaptureAiTargetCallback capture_target,
        ApplyAiTargetCallback apply_target,
        std::function<void()> request_redraw = {},
        std::function<void(const std::string&)> notify_status = {},
        std::function<void(bool, const std::string&)> quick_completion = {});

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);
    bool StartQuickAction(AiActionType action, std::string& error);
    void PrepareQuickActions();
    AiQuickStatusSnapshot QuickStatus() const;
    void StopQuickAction();
    void Poll();

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<AiActionsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
