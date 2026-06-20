#include "assistant_modals.hpp"

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

TtsModalContent::TtsModalContent(const Theme* theme)
    : theme_(theme) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
}

ftxui::Element TtsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        text(" Text-to-Speech") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        StatusLine("Language", "not selected", theme),
        StatusLine("Speaker", "not selected", theme),
        text(""),
        text(" Piper and voice are not configured.") |
            color(theme.modal_text_color),
        text(" Install/download/status controls will be added later.") |
            dim |
            color(theme.modal_text_color),
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

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

AssistantSettingsModalContent::AssistantSettingsModalContent(const Theme* theme)
    : theme_(theme) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
}

ftxui::Element AssistantSettingsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        text(" Assistant Settings") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        text(" Piper TTS") | bold | color(theme.modal_text_color),
        text("  Install: not configured") | color(theme.modal_text_color),
        text("  Download: pending user action") | color(theme.modal_text_color),
        text("  Status: unavailable") | color(theme.modal_text_color),
        text(""),
        text(" Ollama / Gemma") | bold | color(theme.modal_text_color),
        text("  Install: not configured") | color(theme.modal_text_color),
        text("  Download: pending user action") | color(theme.modal_text_color),
        text("  Status: unavailable") | color(theme.modal_text_color),
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

TtsModal::TtsModal(const Theme* theme)
    : theme_(theme) {
    content_ = std::make_shared<TtsModalContent>(theme_);
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Placeholder only. Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component TtsModal::View() const {
    return modal_;
}

void TtsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void TtsModal::Close() {
    open_ = false;
}

bool TtsModal::IsOpen() const {
    return open_;
}

bool TtsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
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

AssistantSettingsModal::AssistantSettingsModal(const Theme* theme)
    : theme_(theme) {
    content_ = std::make_shared<AssistantSettingsModalContent>(theme_);
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
    modal_->SetFooterText("Placeholder only. Escape closes.");
    modal_->SetFooterButtons({{"Close", [this] { Close(); }}});
    modal_->SetBodyFrameScrolling(false);
}

ftxui::Component AssistantSettingsModal::View() const {
    return modal_;
}

void AssistantSettingsModal::Open() {
    open_ = true;
    content_->SetTheme(theme_);
    modal_->SetTheme(theme_);
    content_->GetMainComponent()->TakeFocus();
}

void AssistantSettingsModal::Close() {
    open_ = false;
}

bool AssistantSettingsModal::IsOpen() const {
    return open_;
}

bool AssistantSettingsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
