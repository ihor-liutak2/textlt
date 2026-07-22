#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <functional>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "theme.hpp" // Assuming Theme is available for styling
#include "ui_button.hpp"
#include "modal_interface.hpp" // Include the interface

namespace textlt {

class ModalWindow : public ftxui::ComponentBase {
public:
    // Callback for when the modal is requested to be closed.
    using OnCloseCallback = std::function<void()>;
    using ButtonCallback = std::function<void()>;
    using ButtonEnabledCallback = std::function<bool()>;

    struct FooterButton {
        std::string label;
        ButtonCallback on_click;
        ButtonRole role = ButtonRole::Default;
        ButtonEnabledCallback enabled;
    };

    ModalWindow(std::shared_ptr<IModalContent> content, const Theme* theme, OnCloseCallback on_close);

    // ComponentBase overrides
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    // Set the theme for rendering
    void SetTheme(const Theme* new_theme) { theme_ = new_theme; }
    void SetHeaderVisible(bool visible) { show_header_ = visible; }
    void SetFooterVisible(bool visible);
    void SetHeaderCloseVisible(bool visible);
    void SetFooterText(std::string text);
    void SetFooterButtons(std::vector<FooterButton> buttons);
    void SetBodyHeightLimit(int rows) { max_body_height_ = rows; }
    void SetBodyWidth(int columns);
    void SetModalSize(int columns, int rows);
    void SetBodyFrameScrolling(bool enabled) { body_frame_scrolling_ = enabled; }
    void RefreshChildren() { RebuildChildren(); }

private:
    ftxui::ButtonOption MakeTextButtonOption(
        const std::string& label,
        ButtonCallback callback,
        ButtonRole role = ButtonRole::Default,
        ButtonEnabledCallback enabled = {}) const;
    void RebuildChildren();
    ftxui::Component ActiveChild() override;
    void SetActiveChild(ftxui::ComponentBase* child) override;
    void MoveActionFocus(int delta);
    bool IsChildEnabled(size_t child_index) const;
    ftxui::Element RenderHeader(const Theme& theme);
    ftxui::Element RenderFooter(const Theme& theme);
    ftxui::Element RenderBody(const Theme& theme);
    ModalFrameStyle FrameStyle() const;
    int ModalWidth() const;
    int ModalHeight() const;
    int BodyHeight() const;
    int BodyWidth() const;

    std::shared_ptr<IModalContent> content_;
    const Theme* theme_;
    OnCloseCallback on_close_;
    ftxui::Component header_close_button_;
    std::vector<ftxui::Component> footer_buttons_;
    std::vector<ButtonEnabledCallback> footer_button_enabled_;
    std::string footer_text_;
    bool show_header_ = true;
    bool show_footer_ = true;
    bool show_header_close_ = true;
    int max_body_height_ = 18;
    int modal_width_ = 60;
    int modal_height_ = 0;
    bool modal_size_overridden_ = false;
    size_t active_child_index_ = 0;
    bool body_frame_scrolling_ = true;
    ftxui::Box body_box_;
};

} // namespace textlt
