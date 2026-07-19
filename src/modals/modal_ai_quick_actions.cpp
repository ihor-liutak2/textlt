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
    ButtonRole role,
    std::function<bool()> enabled,
    std::function<void()> on_click) {
    ButtonSpec spec = ButtonSpecFromLabel(
        std::move(label), role, ButtonVariant::AccentEdges, ButtonSize::Compact);
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = [enabled, on_click = std::move(on_click)] {
        if (!enabled || enabled()) {
            on_click();
        }
    };
    option.transform = [theme, spec, enabled = std::move(enabled)](
                           const ftxui::EntryState& state) mutable {
        const Theme& resolved = theme && *theme ? **theme : FallbackTheme();
        spec.enabled = !enabled || enabled();
        return RenderModalFlatButton(resolved, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

} // namespace

AiQuickActionsModalContent::AiQuickActionsModalContent(
    const Theme* theme,
    RunCallback run_callback,
    StatusCallback status_callback,
    StopCallback stop_callback,
    CloseCallback close_callback)
    : theme_(theme),
      run_callback_(std::move(run_callback)),
      status_callback_(std::move(status_callback)),
      stop_callback_(std::move(stop_callback)),
      close_callback_(std::move(close_callback)) {
    translate_button_ = MakeQuickButton(
        &theme_, "1 Translate", ButtonRole::Primary,
        [this] {
            const AiQuickStatusSnapshot status = Status();
            return status.ready && !status.busy && !status.checking;
        },
        [this] { Trigger(AiActionType::Translate); });
    edit_button_ = MakeQuickButton(
        &theme_, "2 Edit", ButtonRole::Primary,
        [this] {
            const AiQuickStatusSnapshot status = Status();
            return status.ready && !status.busy && !status.checking;
        },
        [this] { Trigger(AiActionType::Edit); });
    stop_button_ = MakeQuickButton(
        &theme_, "3 Stop", ButtonRole::Warning,
        [this] {
            const AiQuickStatusSnapshot status = Status();
            return status.busy && !status.stopping;
        },
        [this] { Stop(); });

    auto buttons = ftxui::Container::Horizontal({
        translate_button_, edit_button_, stop_button_,
    });
    container_ = ftxui::CatchEvent(buttons, [this](ftxui::Event event) {
        return HandleEvent(std::move(event));
    });
}

AiQuickStatusSnapshot AiQuickActionsModalContent::Status() const {
    return status_callback_ ? status_callback_() : AiQuickStatusSnapshot{};
}

void AiQuickActionsModalContent::Open() {
    error_message_.clear();
    spinner_frame_ = 0;
    last_busy_ = false;
    last_checking_ = false;
    if (translate_button_) {
        translate_button_->TakeFocus();
    }
}

void AiQuickActionsModalContent::Trigger(AiActionType action) {
    const AiQuickStatusSnapshot status = Status();
    if (status.busy) {
        error_message_ = "An AI action is already running. Press 3 to stop it.";
        return;
    }
    if (status.checking) {
        error_message_ = "Wait until the AI readiness check finishes.";
        return;
    }
    if (!status.ready) {
        error_message_ = status.status_label.empty()
            ? "AI is not ready."
            : status.status_label;
        return;
    }

    std::string error;
    if (!run_callback_ || !run_callback_(action, error)) {
        error_message_ = error.empty() ? "Could not start the AI action." : std::move(error);
        return;
    }
    error_message_.clear();
    if (stop_button_) {
        stop_button_->TakeFocus();
    }
}

void AiQuickActionsModalContent::Stop() {
    const AiQuickStatusSnapshot status = Status();
    if (!status.busy || status.stopping) {
        return;
    }
    error_message_.clear();
    if (stop_callback_) {
        stop_callback_();
    }
}

bool AiQuickActionsModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        const AiQuickStatusSnapshot status = Status();
        if (status.busy) {
            Stop();
        } else if (close_callback_) {
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
    if (MatchesPlainShortcutKey(event, '3')) {
        Stop();
        return true;
    }
    return false;
}

ftxui::Element AiQuickActionsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const AiQuickStatusSnapshot status = Status();
    if (last_busy_ && !status.busy && translate_button_) {
        translate_button_->TakeFocus();
    }
    if (last_checking_ && !status.checking &&
        error_message_ == "Wait until the AI readiness check finishes.") {
        error_message_.clear();
    }
    last_busy_ = status.busy;
    last_checking_ = status.checking;
    if (status.busy || status.checking) {
        ++spinner_frame_;
    }

    Elements rows = {
        hbox({
            text(" Model: ") | bold | color(theme.modal_accent),
            paragraph(status.model_label.empty() ? "Not selected" : status.model_label) |
                color(theme.modal_text_color) | flex,
        }),
        hbox({
            text(" AI:    ") | bold | color(theme.modal_accent),
            paragraph(status.status_label.empty() ? "Not ready" : status.status_label) |
                color(status.ready || status.busy
                    ? theme.modal_text_color
                    : theme.modal_accent) | flex,
        }),
        separator() | color(theme.modal_border),
        hbox({
            filler(),
            translate_button_->Render(),
            text(" "),
            edit_button_->Render(),
            text(" "),
            stop_button_->Render(),
            filler(),
        }),
    };
    if (!error_message_.empty()) {
        rows.push_back(paragraph(error_message_) | color(theme.modal_accent) | bold);
    } else {
        rows.push_back(text(""));
    }
    std::string activity_label;
    if (status.stopping) {
        activity_label = " Stopping AI operation...";
    } else if (status.busy) {
        activity_label = status.active_action == AiActionType::Translate
            ? " Translating current paragraph..."
            : " Editing current paragraph...";
    } else if (status.checking) {
        activity_label = " Checking AI readiness...";
    } else {
        activity_label = status.ready ? " Ready" : " Not ready";
    }
    rows.push_back(separator() | color(theme.modal_border));
    rows.push_back(hbox({
        filler(),
        (status.busy || status.checking)
            ? spinner(0, spinner_frame_) | bold | color(theme.modal_accent)
            : text(" "),
        text(activity_label) | color(theme.modal_text_color),
        filler(),
    }));
    return vbox(std::move(rows)) | color(theme.modal_text_color);
}

AiQuickActionsModal::AiQuickActionsModal(
    const Theme* theme,
    AiQuickActionsModalContent::RunCallback run_callback,
    AiQuickActionsModalContent::StatusCallback status_callback,
    AiQuickActionsModalContent::StopCallback stop_callback,
    AiQuickActionsModalContent::CloseCallback close_callback)
    : theme_(theme) {
    content_ = std::make_shared<AiQuickActionsModalContent>(
        theme_,
        std::move(run_callback),
        std::move(status_callback),
        std::move(stop_callback),
        std::move(close_callback));
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
        modal_->SetModalSize(58, 14);
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
