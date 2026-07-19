#include "modals/modal_ai_quick_actions.hpp"

#include <utility>

#include "ftxui/component/component_options.hpp"
#include "keyboard_shortcuts.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

ftxui::Component MakeQuickButton(
    const Theme** theme,
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = ButtonSpecFromLabel(
        std::move(label),
        ButtonRole::Primary,
        ButtonVariant::AccentEdges,
        ButtonSize::Compact);
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(on_click);
    option.transform = [theme, spec](const ftxui::EntryState& state) {
        const Theme& resolved = theme && *theme ? **theme : FallbackTheme();
        return RenderModalFlatButton(resolved, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

} // namespace

AiQuickActionsModalContent::AiQuickActionsModalContent(
    const Theme* theme,
    RunCallback run_callback,
    CloseCallback close_callback)
    : theme_(theme),
      run_callback_(std::move(run_callback)),
      close_callback_(std::move(close_callback)) {
    translate_button_ = MakeQuickButton(
        &theme_, "1 Translate", [this] { Trigger(AiActionType::Translate); });
    edit_button_ = MakeQuickButton(
        &theme_, "2 Edit", [this] { Trigger(AiActionType::Edit); });

    auto buttons = ftxui::Container::Horizontal({translate_button_, edit_button_});
    container_ = ftxui::CatchEvent(buttons, [this](ftxui::Event event) {
        return HandleEvent(std::move(event));
    });
}

void AiQuickActionsModalContent::Open() {
    error_message_.clear();
    if (translate_button_) {
        translate_button_->TakeFocus();
    }
}

void AiQuickActionsModalContent::Trigger(AiActionType action) {
    std::string error;
    if (!run_callback_ || !run_callback_(action, error)) {
        error_message_ = error.empty() ? "Could not start the AI action." : std::move(error);
        return;
    }
    error_message_.clear();
    if (close_callback_) {
        close_callback_();
    }
}

bool AiQuickActionsModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        if (close_callback_) {
            close_callback_();
        }
        return true;
    }
    if (MatchesPlainShortcutKey(event, '1')) {
        Trigger(AiActionType::Translate);
        return true;
    }
    if (MatchesPlainShortcutKey(event, '2')) {
        Trigger(AiActionType::Edit);
        return true;
    }
    return false;
}

ftxui::Element AiQuickActionsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements rows = {
        paragraph("Use the saved AI settings for the paragraph containing the cursor.") |
            color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        hbox({
            filler(),
            translate_button_->Render(),
            text("   "),
            edit_button_->Render(),
            filler(),
        }),
    };
    if (!error_message_.empty()) {
        rows.push_back(
            paragraph(error_message_) | color(theme.modal_accent) | bold);
    }
    return vbox(std::move(rows)) | color(theme.modal_text_color);
}

AiQuickActionsModal::AiQuickActionsModal(
    const Theme* theme,
    AiQuickActionsModalContent::RunCallback run_callback,
    AiQuickActionsModalContent::CloseCallback close_callback)
    : theme_(theme) {
    content_ = std::make_shared<AiQuickActionsModalContent>(
        theme_, std::move(run_callback), std::move(close_callback));
    modal_ = std::make_shared<ModalWindow>(
        content_, theme_, [this] { Close(); });
}

ftxui::Component AiQuickActionsModal::View() const {
    return modal_;
}

void AiQuickActionsModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
        modal_->SetHeaderCloseVisible(false);
        modal_->SetFooterVisible(false);
        modal_->SetModalSize(44, 10);
    }
}

void AiQuickActionsModal::Close() {
    open_ = false;
}

bool AiQuickActionsModal::IsOpen() const {
    return open_;
}

bool AiQuickActionsModal::OnEvent(ftxui::Event event) {
    return open_ && modal_ && modal_->OnEvent(std::move(event));
}

} // namespace textlt
