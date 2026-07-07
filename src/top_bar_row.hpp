#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "modals/modal_tts.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt {

class TopBarRowComponent : public ftxui::ComponentBase {
public:
    struct Callbacks {
        std::function<void(const std::string& command_id)> run_command;
        std::function<bool()> should_show_tts_controls;
        std::function<std::string()> tts_status;
        std::function<TtsHeaderButton()> active_tts_button;
    };

    TopBarRowComponent(const Theme* theme, Callbacks callbacks);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return ShouldShowTtsControls(); }

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    ftxui::ButtonOption MakeTopButtonOption(
        std::string label,
        std::string command_id,
        TtsHeaderButton active_button,
        ButtonRole role,
        std::string icon);

    bool IsActive(TtsHeaderButton button) const;
    bool ShouldShowTtsControls() const;
    std::string TtsStatus() const;
    ftxui::Element RenderTtsControls();

    const Theme* theme_ = nullptr;
    Callbacks callbacks_;
    ftxui::Component button_container_;
    ftxui::Component open_tts_button_;
    ftxui::Component tts_play_button_;
    ftxui::Component tts_pause_button_;
    ftxui::Component tts_stop_button_;
    ftxui::Component tts_next_button_;
    ftxui::Box box_;
};

} // namespace textlt
