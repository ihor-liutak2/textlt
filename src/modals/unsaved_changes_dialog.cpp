#include "unsaved_changes_dialog.hpp"

#include <utility>

#include "ftxui/dom/elements.hpp"

namespace textlt {

UnsavedChangesContent::UnsavedChangesContent(const Theme* theme, std::string* display_name)
    : theme_(theme),
      display_name_(display_name) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
}

ftxui::Element UnsavedChangesContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string name = display_name_ && !display_name_->empty()
        ? *display_name_
        : "this file";

    return vbox({
        filler(),
        text("Save changes to " + name + " before closing?") |
            color(theme.modal_text_color) |
            center,
        filler(),
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_text_color);
}

UnsavedChangesDialog::UnsavedChangesDialog(const Theme* theme,
                                           Action on_save,
                                           Action on_discard,
                                           Action on_cancel)
    : theme_(theme) {
    content_impl_ = std::make_shared<UnsavedChangesContent>(theme_, &display_name_);
    modal_window_ = std::make_shared<ModalWindow>(
        content_impl_,
        theme_,
        std::move(on_cancel));
    modal_window_->SetBodyFrameScrolling(false);
    modal_window_->SetFooterText("Choose how to continue.");
    modal_window_->SetFooterButtons({
        {"Save", std::move(on_save)},
        {"Don't Save", std::move(on_discard)},
        {"Cancel", std::move(on_cancel)},
    });
}

ftxui::Component UnsavedChangesDialog::View() const {
    return modal_window_;
}

void UnsavedChangesDialog::Open(std::string display_name) {
    display_name_ = std::move(display_name);
    open_ = true;
    SetTheme(theme_);
    TakeFocus();
}

void UnsavedChangesDialog::Close() {
    open_ = false;
}

bool UnsavedChangesDialog::IsOpen() const {
    return open_;
}

void UnsavedChangesDialog::SetTheme(const Theme* theme) {
    theme_ = theme;
    if (content_impl_) {
        content_impl_->SetTheme(theme);
    }
    if (modal_window_) {
        modal_window_->SetTheme(theme);
    }
}

void UnsavedChangesDialog::TakeFocus() {
    if (modal_window_) {
        modal_window_->TakeFocus();
    }
}

} // namespace textlt
