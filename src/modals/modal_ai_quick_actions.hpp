#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ai/ai_prompts.hpp"
#include "ai/ai_quick_status.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class AiQuickActionsModalContent : public IModalContent {
public:
    using RunCallback = std::function<bool(AiActionType action, std::string& error)>;
    using StatusCallback = std::function<AiQuickStatusSnapshot()>;
    using StopCallback = std::function<void()>;
    using CloseCallback = std::function<void()>;

    AiQuickActionsModalContent(
        const Theme* theme,
        RunCallback run_callback,
        StatusCallback status_callback,
        StopCallback stop_callback,
        CloseCallback close_callback);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Quick AI"; }
    ModalSizePreference GetModalSizePreference() const override { return {58, 14}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    bool HandleEvent(ftxui::Event event);

private:
    void Trigger(AiActionType action);
    void Stop();
    AiQuickStatusSnapshot Status() const;

    const Theme* theme_ = nullptr;
    RunCallback run_callback_;
    StatusCallback status_callback_;
    StopCallback stop_callback_;
    CloseCallback close_callback_;
    std::string error_message_;
    int spinner_frame_ = 0;
    bool last_busy_ = false;
    bool last_checking_ = false;

    ftxui::Component translate_button_;
    ftxui::Component edit_button_;
    ftxui::Component stop_button_;
    ftxui::Component container_;
};

class AiQuickActionsModal {
public:
    AiQuickActionsModal(
        const Theme* theme,
        AiQuickActionsModalContent::RunCallback run_callback,
        AiQuickActionsModalContent::StatusCallback status_callback,
        AiQuickActionsModalContent::StopCallback stop_callback,
        AiQuickActionsModalContent::CloseCallback close_callback);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<AiQuickActionsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
