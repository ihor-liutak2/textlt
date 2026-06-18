#include "modal_window.hpp"

namespace textlt {

ModalWindow::ModalWindow(std::shared_ptr<IModalContent> content, const Theme* theme, OnCloseCallback on_close)
    : content_(std::move(content)),
      theme_(theme),
      on_close_(std::move(on_close)) {

    // Create the close button
    close_button_ = ftxui::Button("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    });

    // Add content's main component and the close button to the modal's internal components.
    Add(content_->GetMainComponent());
    Add(close_button_);
}

ftxui::Element ModalWindow::Render() {
    using namespace ftxui;
    const Theme& current_theme = theme_ ? *theme_ : FallbackTheme();

    // Render the content provided by IModalContent
    Element modal_content = content_->Render();

    Element scrollable_content_box =
        vbox({
            modal_content,
        }) | vscroll_indicator | frame
          | bgcolor(current_theme.modal_input_bg) // Use input_bg for content area
          | color(current_theme.modal_input_fg); // Use input_fg for content text

    Element dialog_body =
        vbox({
            scrollable_content_box | size(HEIGHT, LESS_THAN, 18), // Constrain height of content
            separator() | color(current_theme.modal_border),
            hbox({
                filler(),
                close_button_->Render(),
                filler(),
            }),
        });

    // Assemble outer dialog frame
    return window(
        text(" " + content_->GetTitle() + " ") | bold | color(current_theme.modal_accent),
        dialog_body | bgcolor(current_theme.modal_background) | color(current_theme.modal_text_color)
    ) | border
      | bgcolor(current_theme.modal_background) // Forces the background color onto the border lines too
      | color(current_theme.modal_border)
      | clear_under
      | center // Center the modal on the screen.
      | size(WIDTH, GREATER_THAN, 40)
      | size(WIDTH, LESS_THAN, 60); // Constrain width too.
}

bool ModalWindow::OnEvent(ftxui::Event event) {
    // Attempt to handle event with children first
    if (ComponentBase::OnEvent(event)) {
        return true; // Event handled by one of the children (content or close button)
    }

    // If children didn't handle it, check for generic modal close events
    if (event == ftxui::Event::Escape) {
        if (on_close_) {
            on_close_();
        }
        return true; // Event consumed by modal
    }

    // Consume all other events to prevent them from propagating to underlying components.
    return true;
}

} // namespace textlt
