#include "top_bar_row.hpp"

#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ui_button.hpp"

namespace textlt {

namespace {

const Theme& ResolveTheme(const Theme* theme) {
    static const Theme fallback_theme;
    return theme ? *theme : fallback_theme;
}

} // namespace

TopBarRowComponent::TopBarRowComponent(const Theme* theme, Callbacks callbacks)
    : theme_(theme), callbacks_(std::move(callbacks)) {
    open_tts_button_ = ftxui::Button(MakeTopButtonOption(
        "TTS", "tts.open_modal", TtsHeaderButton::Open, ButtonRole::Media, "▶"));
    tts_play_button_ = ftxui::Button(MakeTopButtonOption(
        "Play", "tts.play", TtsHeaderButton::Play, ButtonRole::Media, "▶"));
    tts_pause_button_ = ftxui::Button(MakeTopButtonOption(
        "Pause", "tts.pause", TtsHeaderButton::Pause, ButtonRole::Media, "⏸"));
    tts_stop_button_ = ftxui::Button(MakeTopButtonOption(
        "Stop", "tts.stop", TtsHeaderButton::Stop, ButtonRole::Warning, "■"));
    tts_next_button_ = ftxui::Button(MakeTopButtonOption(
        "Next", "tts.next", TtsHeaderButton::Next, ButtonRole::Media, "⏭"));

    button_container_ = ftxui::Container::Horizontal({
        open_tts_button_,
        tts_play_button_,
        tts_pause_button_,
        tts_stop_button_,
        tts_next_button_,
    });
    Add(button_container_);
}

ftxui::ButtonOption TopBarRowComponent::MakeTopButtonOption(
    std::string label,
    std::string command_id,
    TtsHeaderButton active_button,
    ButtonRole role,
    std::string icon) {
    ButtonSpec base_spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        ButtonVariant::AccentEdges,
        ButtonSize::Compact,
        std::move(icon));

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base_spec);
    option.on_click = [this, command_id = std::move(command_id)] {
        if (callbacks_.run_command) {
            callbacks_.run_command(command_id);
        }
    };
    option.transform = [this, active_button, base_spec](const ftxui::EntryState& state) {
        ButtonSpec spec = base_spec;
        spec.selected = IsActive(active_button);
        return RenderButton(ResolveTheme(theme_), spec, state.focused || state.active);
    };
    return option;
}

bool TopBarRowComponent::IsActive(TtsHeaderButton button) const {
    if (!callbacks_.active_tts_button) {
        return false;
    }
    const TtsHeaderButton active = callbacks_.active_tts_button();
    if (button == TtsHeaderButton::Play) {
        return active == TtsHeaderButton::Play || active == TtsHeaderButton::Test;
    }
    return active == button;
}

bool TopBarRowComponent::ShouldShowTtsControls() const {
    return callbacks_.should_show_tts_controls && callbacks_.should_show_tts_controls();
}

std::string TopBarRowComponent::TtsStatus() const {
    if (!callbacks_.tts_status) {
        return "";
    }
    return callbacks_.tts_status();
}

ftxui::Element TopBarRowComponent::RenderTtsControls() {
    using namespace ftxui;

    if (!ShouldShowTtsControls()) {
        return text("");
    }

    const std::string status = TtsStatus();
    const Theme& theme = ResolveTheme(theme_);
    return hbox({
        text(" TTS: ") | bold | color(theme.menu_foreground),
        text(status.empty() ? "ready" : status) | color(theme.menu_foreground),
        text("  "),
        open_tts_button_ ? open_tts_button_->Render() : text("[TTS]"),
        text(" "),
        tts_play_button_ ? tts_play_button_->Render() : text("[Play]"),
        text(" "),
        tts_pause_button_ ? tts_pause_button_->Render() : text("[Pause]"),
        text(" "),
        tts_stop_button_ ? tts_stop_button_->Render() : text("[Stop]"),
        text(" "),
        tts_next_button_ ? tts_next_button_->Render() : text("[Next]"),
    });
}

ftxui::Element TopBarRowComponent::Render() {
    using namespace ftxui;

    const Theme& theme = ResolveTheme(theme_);
    return hbox({
        text(" textlt v1.0.0 - Native Non-Modal Text Editor") |
            bold |
            color(theme.menu_foreground),
        filler(),
        RenderTtsControls(),
    }) | reflect(box_);
}

bool TopBarRowComponent::OnEvent(ftxui::Event event) {
    if (!ShouldShowTtsControls()) {
        return false;
    }

    if (event.is_mouse()) {
        auto& mouse = event.mouse();
        if (!box_.Contain(mouse.x, mouse.y)) {
            return false;
        }
        if (button_container_) {
            button_container_->TakeFocus();
            if (button_container_->OnEvent(event)) {
                return true;
            }
        }
        return mouse.button == ftxui::Mouse::Left;
    }

    return button_container_ && button_container_->OnEvent(event);
}

} // namespace textlt
