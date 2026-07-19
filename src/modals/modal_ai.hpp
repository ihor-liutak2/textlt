#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ai/ai_backend.hpp"
#include "editor/document_session.hpp"
#include "editor_config.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
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
        std::function<void()> request_redraw = {});
    ~AiActionsModalContent() override;

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "AI Actions"; }
    ModalSizePreference GetModalSizePreference() const override { return {76, 20}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override;

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromConfig();
    bool HandleShortcut(ftxui::Event event);
    void PrepareClose();

private:
    void SetEditStyle(AiEditStyle style);
    void StartAction(AiActionType action);
    void ApplyPendingResult();
    void PersistOptions();
    void RequestRedraw() const;
    std::string SelectedModelLabel() const;
    std::string SelectedBackendLabel() const;
    ftxui::Element RenderContent(const Theme& theme);

    const Theme* theme_ = nullptr;
    EditorConfig* config_ = nullptr;
    CaptureAiTargetCallback capture_target_;
    ApplyAiTargetCallback apply_target_;
    std::function<void()> request_redraw_;

    std::string language_ = "Ukrainian";
    int language_cursor_ = 0;
    AiEditStyle edit_style_ = AiEditStyle::Conversational;
    bool whole_document_ = false;
    bool persisted_whole_document_ = false;
    std::string persisted_language_;
    AiEditStyle persisted_edit_style_ = AiEditStyle::Conversational;

    mutable std::mutex state_mutex_;
    std::thread worker_;
    bool busy_ = false;
    std::string status_ = "Ready";
    bool pending_result_ = false;
    bool discard_result_ = false;
    AiBackendResult pending_backend_result_;
    AiDocumentTarget pending_target_;
    AiActionType pending_action_ = AiActionType::Translate;

    ftxui::Component language_input_;
    ftxui::Component conversational_button_;
    ftxui::Component business_button_;
    ftxui::Component whole_document_checkbox_;
    ftxui::Component translate_button_;
    ftxui::Component edit_button_;
    ftxui::Component container_;
};

class AiActionsModal {
public:
    AiActionsModal(
        const Theme* theme,
        EditorConfig* config,
        CaptureAiTargetCallback capture_target,
        ApplyAiTargetCallback apply_target,
        std::function<void()> request_redraw = {});

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

} // namespace textlt
