#pragma once

#include <memory>
#include <string>
#include <functional>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp" // Assuming Theme is available for styling
#include "modal_interface.hpp" // Include the interface

namespace textlt {

class ModalWindow : public ftxui::ComponentBase {
public:
    // Callback for when the modal is requested to be closed.
    using OnCloseCallback = std::function<void()>;

    ModalWindow(std::shared_ptr<IModalContent> content, const Theme* theme, OnCloseCallback on_close);

    // ComponentBase overrides
    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    // Set the theme for rendering
    void SetTheme(const Theme* new_theme) { theme_ = new_theme; }

private:
    std::shared_ptr<IModalContent> content_;
    const Theme* theme_;
    OnCloseCallback on_close_;
    ftxui::Component close_button_;
};

} // namespace textlt
