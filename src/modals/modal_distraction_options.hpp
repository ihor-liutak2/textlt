#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"

#include "distraction_mode_controller.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"
#include "ui_button.hpp"

namespace textlt {

class DistractionOptionsContent : public IModalContent {
public:
    using SettingsProvider = std::function<DistractionModeSettings()>;
    using ApplySettingsCallback = std::function<void(DistractionModeSettings)>;
    using CommandCallback = std::function<void(const std::string& command_id)>;

    DistractionOptionsContent(
        const Theme* theme,
        SettingsProvider settings_provider,
        ApplySettingsCallback on_apply_settings,
        CommandCallback on_command);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Distraction Mode"; }
    ModalSizePreference GetModalSizePreference() const override { return {74, 18}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void RefreshFromApp();
    void TakeFocus();

private:
    ftxui::ButtonOption MakeButtonOption(
        std::string label,
        std::function<void()> on_click,
        ButtonRole role = ButtonRole::Default,
        std::string icon = {}) const;

    ftxui::Element RenderModeTab(const Theme& theme);
    ftxui::Element RenderLayoutTab(const Theme& theme);
    void SetColumnCount(int column_count);
    void SyncInputsFromDraft();
    void ApplyInputsToDraft();
    void ApplyDraft();
    int ParseInput(const std::string& value, int fallback) const;

    const Theme* theme_ = nullptr;
    SettingsProvider settings_provider_;
    ApplySettingsCallback on_apply_settings_;
    CommandCallback on_command_;
    DistractionModeSettings draft_;
    std::string status_ = "Configure reading layout, then Enter to enable.";

    int active_tab_index_ = 0;
    std::vector<std::string> tabs_ = {"Mode", "Layout"};

    std::string column_width_input_ = "92";
    std::string column_gap_input_ = "6";
    std::string top_padding_input_ = "1";
    std::string bottom_padding_input_ = "1";
    int column_width_cursor_ = 0;
    int column_gap_cursor_ = 0;
    int top_padding_cursor_ = 0;
    int bottom_padding_cursor_ = 0;

    ftxui::Component tab_toggle_;
    ftxui::Component one_column_button_;
    ftxui::Component two_column_button_;
    ftxui::Component enter_button_;
    ftxui::Component exit_button_;
    ftxui::Component apply_button_;
    ftxui::Component column_width_input_component_;
    ftxui::Component column_gap_input_component_;
    ftxui::Component top_padding_input_component_;
    ftxui::Component bottom_padding_input_component_;
    ftxui::Component mode_container_;
    ftxui::Component layout_container_;
    ftxui::Component tabs_container_;
    ftxui::Component container_;
};

class DistractionOptionsModal {
public:
    using SettingsProvider = DistractionOptionsContent::SettingsProvider;
    using ApplySettingsCallback = DistractionOptionsContent::ApplySettingsCallback;
    using CommandCallback = DistractionOptionsContent::CommandCallback;
    using CloseCallback = std::function<void()>;

    DistractionOptionsModal(
        const Theme* theme,
        SettingsProvider settings_provider,
        ApplySettingsCallback on_apply_settings,
        CommandCallback on_command,
        CloseCallback on_close);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    void TakeFocus();
    void SetTheme(const Theme* theme);

private:
    void RequestClose();

    bool open_ = false;
    const Theme* theme_ = nullptr;
    CloseCallback on_close_;
    std::shared_ptr<DistractionOptionsContent> content_;
    std::shared_ptr<ModalWindow> modal_window_;
};

} // namespace textlt
