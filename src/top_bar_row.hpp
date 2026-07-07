#pragma once

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "distraction_mode_controller.hpp"
#include "modals/modal_tts.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt {

class TopBarRowComponent : public ftxui::ComponentBase {
public:
    enum class Mode {
        Normal,
        Distraction,
    };

    struct Callbacks {
        std::function<void(const std::string& command_id)> run_command;
        std::function<bool()> should_show_tts_controls;
        std::function<std::string()> tts_status;
        std::function<TtsHeaderButton()> active_tts_button;
        std::function<bool()> is_distraction_mode;
        std::function<DistractionTopBarState()> distraction_state;
        std::function<void(const std::string& page_input)> set_distraction_page_input;
    };

    TopBarRowComponent(const Theme* theme, Callbacks callbacks);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;
    bool DistractionPagePopupOpen() const;

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    ftxui::ButtonOption MakeTtsButtonOption(
        std::string label,
        std::string command_id,
        TtsHeaderButton active_button,
        ButtonRole role,
        std::string icon);
    ftxui::ButtonOption MakeCommandButtonOption(
        std::string label,
        std::string command_id,
        ButtonRole role,
        std::string icon = {});
    ftxui::ButtonOption MakeActionButtonOption(
        std::string label,
        std::function<void()> on_click,
        ButtonRole role,
        std::string icon = {});

    Mode CurrentMode() const;
    ftxui::Component ActiveButtonContainer() const;
    void RunCommand(const std::string& command_id) const;
    bool IsActive(TtsHeaderButton button) const;
    bool ShouldShowTtsControls() const;
    std::string TtsStatus() const;
    DistractionTopBarState CurrentDistractionState() const;
    void SyncDistractionInputFromState();
    void OpenPagePopup();
    void ClosePagePopup();
    void SubmitPagePopup();
    void NormalizePagePopupInput(bool allow_empty);
    void AppendPagePopupDigit(char digit);
    void BackspacePagePopupInput();
    bool HandlePagePopupKeyEvent(const ftxui::Event& event);
    ftxui::Element RenderTtsControls();
    ftxui::Element RenderDistractionControls();
    ftxui::Element RenderPagePopup();
    ftxui::Element RenderPagePopupInput();

    const Theme* theme_ = nullptr;
    Callbacks callbacks_;
    ftxui::Component tts_button_container_;
    ftxui::Component distraction_button_container_;
    ftxui::Component open_tts_button_;
    ftxui::Component tts_play_button_;
    ftxui::Component tts_pause_button_;
    ftxui::Component tts_stop_button_;
    ftxui::Component tts_next_button_;
    ftxui::Component distraction_prev_button_;
    ftxui::Component distraction_next_button_;
    ftxui::Component distraction_page_button_;
    ftxui::Component distraction_exit_button_;
    ftxui::Component page_popup_container_;
    ftxui::Component page_popup_go_button_;
    ftxui::Component page_popup_cancel_button_;
    std::string distraction_page_input_text_ = "1";
    int distraction_page_input_cursor_ = 0;
    bool page_popup_open_ = false;
    bool page_popup_replace_on_type_ = false;
    ftxui::Box box_;
};

} // namespace textlt
