#include "modal_ai.hpp"

#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

ftxui::Element StatusLine(const std::string& label,
                          const std::string& value,
                          const Theme& theme) {
    using namespace ftxui;
    return hbox({
        text(" " + label + ": ") | bold | color(theme.modal_accent),
        text(value) | color(theme.modal_text_color),
    });
}

} // namespace

AiActionsModalContent::AiActionsModalContent(const Theme* theme)
    : theme_(theme) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
}

ftxui::Element AiActionsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        text(" AI Actions") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        StatusLine("Model", "gemma3:1b", theme),
        text(""),
        text(" Actions") | bold | color(theme.modal_text_color),
        text("  - improve") | color(theme.modal_text_color),
        text("  - translate") | color(theme.modal_text_color),
        text("  - summarize") | color(theme.modal_text_color),
        text("  - explain") | color(theme.modal_text_color),
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

AiActionsModal::AiActionsModal(const Theme* theme)
    : theme_(theme) {
    content_ = std::make_shared<AiActionsModalContent>(theme_);
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Placeholder only. Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AiActionsModal::View() const {
    return modal_;
}

void AiActionsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void AiActionsModal::Close() {
    open_ = false;
}

bool AiActionsModal::IsOpen() const {
    return open_;
}

bool AiActionsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
