#pragma once

#include <functional>
#include <memory>
#include <string>

#include "ai/ai_prompts.hpp"
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
    using CloseCallback = std::function<void()>;

    AiQuickActionsModalContent(
        const Theme* theme,
        RunCallback run_callback,
        CloseCallback close_callback);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Quick AI"; }
    ModalSizePreference GetModalSizePreference() const override { return {44, 10}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return {}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    bool HandleEvent(ftxui::Event event);

private:
    void Trigger(AiActionType action);

    const Theme* theme_ = nullptr;
    RunCallback run_callback_;
    CloseCallback close_callback_;
    std::string error_message_;

    ftxui::Component translate_button_;
    ftxui::Component edit_button_;
    ftxui::Component container_;
};

class AiQuickActionsModal {
public:
    AiQuickActionsModal(
        const Theme* theme,
        AiQuickActionsModalContent::RunCallback run_callback,
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
