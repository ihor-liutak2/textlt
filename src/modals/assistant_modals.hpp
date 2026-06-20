#pragma once

#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

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
    explicit AssistantSettingsModalContent(const Theme* theme);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override { return "Assistant Settings"; }
    ModalSizePreference GetModalSizePreference() const override { return {64, 18}; }

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    const Theme* theme_ = nullptr;
    ftxui::Component renderer_;
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
    explicit AssistantSettingsModal(const Theme* theme);

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
