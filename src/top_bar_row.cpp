#include "top_bar_row.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
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

std::string FormatPageNumber(size_t value) {
    std::ostringstream output;
    output << std::setw(4) << std::setfill('0') << std::max<size_t>(value, 1);
    return output.str();
}

} // namespace

TopBarRowComponent::TopBarRowComponent(const Theme* theme, Callbacks callbacks)
    : theme_(theme), callbacks_(std::move(callbacks)) {
    open_tts_button_ = ftxui::Button(MakeTtsButtonOption(
        "TTS", "tts.open_modal", TtsHeaderButton::Open, ButtonRole::Media, "▶"));
    tts_play_button_ = ftxui::Button(MakeTtsButtonOption(
        "Play", "tts.play", TtsHeaderButton::Play, ButtonRole::Media, "▶"));
    tts_pause_button_ = ftxui::Button(MakeTtsButtonOption(
        "Pause", "tts.pause", TtsHeaderButton::Pause, ButtonRole::Media, "⏸"));
    tts_stop_button_ = ftxui::Button(MakeTtsButtonOption(
        "Stop", "tts.stop", TtsHeaderButton::Stop, ButtonRole::Warning, "■"));
    tts_next_button_ = ftxui::Button(MakeTtsButtonOption(
        "Next", "tts.next", TtsHeaderButton::Next, ButtonRole::Media, "⏭"));

    tts_button_container_ = ftxui::Container::Horizontal({
        open_tts_button_,
        tts_play_button_,
        tts_pause_button_,
        tts_stop_button_,
        tts_next_button_,
    });
    Add(tts_button_container_);

    distraction_prev_button_ = ftxui::Button(MakeCommandButtonOption(
        "Prev", "distraction.previous_page", ButtonRole::Navigation, "◀"));
    distraction_next_button_ = ftxui::Button(MakeCommandButtonOption(
        "Next", "distraction.next_page", ButtonRole::Navigation, "▶"));
    ButtonSpec page_button_spec = ButtonSpecFromLabel(
        "Page", ButtonRole::Navigation, ButtonVariant::Minimal, ButtonSize::Compact);
    ftxui::ButtonOption page_button_option = ftxui::ButtonOption::Simple();
    page_button_option.label = ButtonCaptionText(page_button_spec);
    page_button_option.on_click = [this] {
        if (callbacks_.open_distraction_page_popup) {
            callbacks_.open_distraction_page_popup();
        }
    };
    page_button_option.transform = [this, page_button_spec](const ftxui::EntryState& state) {
        ButtonSpec spec = page_button_spec;
        const DistractionTopBarState page_state = CurrentDistractionState();
        spec.caption = "Page " + FormatPageNumber(page_state.current_page) + "/" +
            FormatPageNumber(page_state.total_pages);
        return RenderModalFlatButton(ResolveTheme(theme_), spec, state.focused || state.active);
    };
    distraction_page_button_ = ftxui::Button(page_button_option);
    distraction_exit_button_ = ftxui::Button(MakeCommandButtonOption(
        "Exit", "distraction.exit", ButtonRole::Cancel));

    distraction_button_container_ = ftxui::Container::Horizontal({
        distraction_prev_button_,
        distraction_next_button_,
        distraction_page_button_,
        distraction_exit_button_,
    });
    Add(distraction_button_container_);
}

ftxui::ButtonOption TopBarRowComponent::MakeTtsButtonOption(
    std::string label,
    std::string command_id,
    TtsHeaderButton active_button,
    ButtonRole role,
    std::string icon) {
    ButtonSpec base_spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        ButtonVariant::Minimal,
        ButtonSize::Compact,
        std::move(icon));

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base_spec);
    option.on_click = [this, command_id = std::move(command_id)] { RunCommand(command_id); };
    option.transform = [this, active_button, base_spec](const ftxui::EntryState& state) {
        ButtonSpec spec = base_spec;
        spec.selected = IsActive(active_button);
        return RenderModalFlatButton(ResolveTheme(theme_), spec, state.focused || state.active);
    };
    return option;
}

ftxui::ButtonOption TopBarRowComponent::MakeCommandButtonOption(
    std::string label,
    std::string command_id,
    ButtonRole role,
    std::string icon) {
    ButtonSpec base_spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        ButtonVariant::Minimal,
        ButtonSize::Compact,
        std::move(icon));

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base_spec);
    option.on_click = [this, command_id = std::move(command_id)] { RunCommand(command_id); };
    option.transform = [this, base_spec](const ftxui::EntryState& state) {
        return RenderModalFlatButton(ResolveTheme(theme_), base_spec, state.focused || state.active);
    };
    return option;
}

ftxui::ButtonOption TopBarRowComponent::MakeActionButtonOption(
    std::string label,
    std::function<void()> on_click,
    ButtonRole role,
    std::string icon) {
    ButtonSpec base_spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        ButtonVariant::Minimal,
        ButtonSize::Compact,
        std::move(icon));

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base_spec);
    option.on_click = std::move(on_click);
    option.transform = [this, base_spec](const ftxui::EntryState& state) {
        return RenderModalFlatButton(ResolveTheme(theme_), base_spec, state.focused || state.active);
    };
    return option;
}

TopBarRowComponent::Mode TopBarRowComponent::CurrentMode() const {
    if (callbacks_.is_distraction_mode && callbacks_.is_distraction_mode()) {
        return Mode::Distraction;
    }
    return Mode::Normal;
}

ftxui::Component TopBarRowComponent::ActiveButtonContainer() const {
    if (CurrentMode() == Mode::Distraction) {
        return distraction_button_container_;
    }
    return tts_button_container_;
}

void TopBarRowComponent::RunCommand(const std::string& command_id) const {
    if (callbacks_.run_command) {
        callbacks_.run_command(command_id);
    }
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
    return CurrentMode() == Mode::Normal &&
        callbacks_.should_show_tts_controls &&
        callbacks_.should_show_tts_controls();
}

std::string TopBarRowComponent::TtsStatus() const {
    if (!callbacks_.tts_status) {
        return "";
    }
    return callbacks_.tts_status();
}

DistractionTopBarState TopBarRowComponent::CurrentDistractionState() const {
    if (!callbacks_.distraction_state) {
        return DistractionTopBarState{};
    }
    return callbacks_.distraction_state();
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

ftxui::Element TopBarRowComponent::RenderDistractionControls() {
    using namespace ftxui;

    const Theme& theme = ResolveTheme(theme_);
    return hbox({
        text(" Distraction: ") | bold | color(theme.menu_foreground),
        distraction_prev_button_ ? distraction_prev_button_->Render() : text("[Prev]"),
        text(" "),
        distraction_next_button_ ? distraction_next_button_->Render() : text("[Next]"),
        text(" "),
        distraction_page_button_ ? distraction_page_button_->Render() : text("[Page]"),
        text(" "),
        distraction_exit_button_ ? distraction_exit_button_->Render() : text("[Exit]"),
    });
}

ftxui::Element TopBarRowComponent::Render() {
    using namespace ftxui;

    const Theme& theme = ResolveTheme(theme_);
    const bool distraction_mode = CurrentMode() == Mode::Distraction;
    if (distraction_mode) {
        return hbox({
            filler(),
            RenderDistractionControls(),
        }) | bgcolor(theme.menu_background) | reflect(box_);
    }

    return hbox({
        text(" textlt v1.0.0 - Native Non-Modal Text Editor") |
            bold |
            color(theme.menu_foreground),
        filler(),
        RenderTtsControls(),
    }) | reflect(box_);
}

bool TopBarRowComponent::Focusable() const {
    return CurrentMode() == Mode::Distraction || ShouldShowTtsControls();
}

bool TopBarRowComponent::OnEvent(ftxui::Event event) {
    if (!Focusable()) {
        return false;
    }

    const auto container = ActiveButtonContainer();
    if (event.is_mouse()) {
        auto& mouse = event.mouse();
        if (!box_.Contain(mouse.x, mouse.y)) {
            return false;
        }
        TakeFocus();
        if (container) {
            container->TakeFocus();
            if (container->OnEvent(event)) {
                return true;
            }
        }
        return mouse.button == ftxui::Mouse::Left;
    }


    return container && container->OnEvent(event);
}

} // namespace textlt
