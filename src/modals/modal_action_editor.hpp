#pragma once

#include <array>
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
    ModalSizePreference GetModalSizePreference() const override { return {102, 42}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return status_; }
    bool FooterTextAboveButtons() const override { return !status_.empty(); }

    void Begin(const actions::ActionItem* item = nullptr);
    bool EditingExisting() const { return !id_.empty(); }
    bool Submit();
    bool HandleEvent(ftxui::Event event);
    void FocusFirstField() { title_input_->TakeFocus(); }
    void ClearStatus() { status_.clear(); }

private:
    friend class ActionEditorModalTestAccess;
    void SelectTab(int index);
    ftxui::Element RenderContentHorizontalScrollbar(size_t tab,
        const Theme& theme);
    void ScrollContentToOffset(size_t tab, size_t offset);

    const Theme* theme_ = nullptr;
    SubmitCallback on_submit_;
    std::string id_;
    std::string title_;
    int active_tab_ = 0;
    std::array<std::string, actions::kMaxActionTabs> captions_;
    std::array<std::string, actions::kMaxActionTabs> contents_;
    std::array<bool, actions::kMaxActionTabs> hide_text_{};
    std::string status_;
    int title_cursor_ = 0;
    std::array<int, actions::kMaxActionTabs> caption_cursors_{};
    std::array<int, actions::kMaxActionTabs> content_cursors_{};
    ftxui::Component title_input_;
    std::array<ftxui::Component, actions::kMaxActionTabs> caption_inputs_;
    std::array<ftxui::Component, actions::kMaxActionTabs> content_inputs_;
    std::array<ftxui::Component, actions::kMaxActionTabs> hide_text_checkboxes_;
    std::array<ftxui::Component, actions::kMaxActionTabs> tab_forms_;
    std::array<ftxui::Component, actions::kMaxActionTabs> tab_buttons_;
    ftxui::Component tab_button_row_;
    ftxui::Component tab_container_;
    ftxui::Component container_;
    ftxui::Box title_clear_box_;
    std::array<ftxui::Box, actions::kMaxActionTabs> caption_clear_boxes_;
    std::array<ftxui::Box, actions::kMaxActionTabs> content_clear_boxes_;
    std::array<ftxui::Box, actions::kMaxActionTabs> content_input_boxes_;
    std::array<ftxui::Box, actions::kMaxActionTabs> content_scrollbar_boxes_;
    std::array<size_t, actions::kMaxActionTabs> content_scroll_offsets_{};
    std::array<ftxui::Box, actions::kMaxActionTabs> tab_boxes_;
    bool content_scrollbar_dragging_ = false;
    size_t content_scrollbar_drag_tab_ = 0;
    int content_scrollbar_drag_thumb_start_ = 0;
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
