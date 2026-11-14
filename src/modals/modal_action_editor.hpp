#pragma once

#include <functional>
#include <memory>
#include <string>

#include "actions/action_model.hpp"
#include "ftxui/component/component.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class ActionEditorModalContent : public IModalContent {
public:
    using SubmitCallback = std::function<bool(
        const actions::ActionItem&, std::string& error)>;

    ActionEditorModalContent(const Theme* theme, SubmitCallback on_submit);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override;
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {82, 27}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return status_; }
    bool FooterTextAboveButtons() const override { return !status_.empty(); }

    void Begin(std::string id, std::string title, std::string description,
        std::string content);
    bool EditingExisting() const { return !id_.empty(); }
    bool Submit();
    void FocusFirstField() { title_input_->TakeFocus(); }
    void ClearStatus() { status_.clear(); }

private:
    friend class ActionEditorModalTestAccess;
    const Theme* theme_ = nullptr;
    SubmitCallback on_submit_;
    std::string id_;
    std::string title_;
    std::string description_;
    std::string content_;
    std::string status_;
    int title_cursor_ = 0;
    int description_cursor_ = 0;
    int content_cursor_ = 0;
    ftxui::Component title_input_;
    ftxui::Component description_input_;
    ftxui::Component content_input_;
    ftxui::Component container_;
};

class ActionEditorModal {
public:
    using SubmitCallback = ActionEditorModalContent::SubmitCallback;
    ActionEditorModal(const Theme* theme, SubmitCallback on_submit);

    ftxui::Component View() const { return modal_; }
    void Open(const actions::ActionItem* item = nullptr);
    void Close();
    bool IsOpen() const { return open_; }
    bool OnEvent(ftxui::Event event);

private:
    friend class ActionEditorModalTestAccess;
    void ConfigureFooter();
    bool open_ = false;
    std::shared_ptr<ActionEditorModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
