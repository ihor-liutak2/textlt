#include "modals/modal_git.hpp"

#include <utility>

#include "ftxui/dom/elements.hpp"

namespace textlt {

void GitModalContent::RequestConfirm(
    const std::string& title,
    const std::string& message,
    const std::string& command_preview,
    std::function<void()> on_confirm,
    const std::string& required_text) {
    confirm_active_ = true;
    confirm_title_ = title;
    confirm_message_ = message;
    confirm_command_preview_ = command_preview;
    confirm_required_text_ = required_text;
    confirm_typed_text_.clear();
    confirm_action_ = std::move(on_confirm);

    if (!confirm_required_text_.empty() && confirm_input_component_) {
        confirm_input_component_->TakeFocus();
    } else if (confirm_confirm_button_) {
        confirm_confirm_button_->TakeFocus();
    }
}

void GitModalContent::ConfirmPendingAction() {
    if (!confirm_active_) {
        return;
    }
    if (!confirm_required_text_.empty() && confirm_typed_text_ != confirm_required_text_) {
        status_ = "Confirmation text does not match.";
        return;
    }

    auto action = std::move(confirm_action_);
    confirm_active_ = false;
    confirm_title_.clear();
    confirm_message_.clear();
    confirm_command_preview_.clear();
    confirm_required_text_.clear();
    confirm_typed_text_.clear();

    if (action) {
        action();
    }
}

void GitModalContent::CancelPendingAction() {
    confirm_active_ = false;
    confirm_action_ = nullptr;
    confirm_title_.clear();
    confirm_message_.clear();
    confirm_command_preview_.clear();
    confirm_required_text_.clear();
    confirm_typed_text_.clear();
    status_ = "Git action cancelled.";
}

bool GitModalContent::HandleConfirmEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        CancelPendingAction();
        return true;
    }
    if (event == ftxui::Event::Return && confirm_required_text_.empty()) {
        ConfirmPendingAction();
        return true;
    }
    return confirm_container_ ? confirm_container_->OnEvent(event) : true;
}

ftxui::Element GitModalContent::RenderConfirmOverlay() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    ftxui::Elements rows;
    rows.push_back(ftxui::text(confirm_title_.empty() ? "Confirm Git action" : confirm_title_) |
                   ftxui::bold | ftxui::color(theme.modal_accent));
    rows.push_back(ftxui::separator() | ftxui::color(theme.modal_border));
    rows.push_back(ftxui::text(confirm_message_) | ftxui::color(theme.modal_text_color));
    rows.push_back(ftxui::text(""));
    rows.push_back(ftxui::text("Command:") | ftxui::bold | ftxui::color(theme.modal_text_color));
    rows.push_back(ftxui::text(confirm_command_preview_) | ftxui::color(theme.modal_text_color));

    if (!confirm_required_text_.empty()) {
        rows.push_back(ftxui::text(""));
        rows.push_back(ftxui::text("Type " + confirm_required_text_ + " to confirm:") |
                       ftxui::bold | ftxui::color(theme.modal_text_color));
        rows.push_back(confirm_input_component_->Render() |
                       ftxui::bgcolor(theme.modal_input_bg) |
                       ftxui::color(theme.modal_input_fg) |
                       ftxui::frame);
    }

    rows.push_back(ftxui::text(""));
    rows.push_back(ftxui::hbox({
        ftxui::filler(),
        confirm_confirm_button_->Render(),
        ftxui::text(" "),
        confirm_cancel_button_->Render(),
    }));

    return ftxui::vbox(std::move(rows)) |
           ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 84) |
           ftxui::border |
           ftxui::bgcolor(theme.modal_background) |
           ftxui::color(theme.modal_foreground);
}

} // namespace textlt
