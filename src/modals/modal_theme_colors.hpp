#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"

#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class ThemeColorsModalContent : public IModalContent {
public:
    using ApplyCallback = std::function<bool(
        const std::map<std::string, std::string>& overrides,
        std::string& error)>;
    using ResetCallback = std::function<bool(std::string& error)>;

    ThemeColorsModalContent(
        const Theme* theme,
        ApplyCallback apply_callback,
        ResetCallback reset_callback);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Theme Color Settings"; }
    ModalSizePreference GetModalSizePreference() const override { return {84, 31}; }
    std::string GetFooterText() const override { return status_; }
    bool FooterTextAboveButtons() const override { return true; }

    void Open();
    bool Apply();
    void ResetToDefault();

private:
    struct ColorField {
        std::string key;
        std::string label;
        ftxui::Color Theme::* member = nullptr;
        int group = 0;
        std::string value;
        int cursor = 0;
    };

    void BuildFields();
    void BuildComponents();
    void LoadValuesFromTheme();
    void SelectGroup(int group);
    ftxui::Element RenderTabs() const;
    ftxui::Element RenderGroup() const;

    const Theme* theme_ = nullptr;
    ApplyCallback apply_callback_;
    ResetCallback reset_callback_;
    std::vector<ColorField> fields_;
    std::vector<ftxui::Component> inputs_;
    std::vector<ftxui::Component> group_components_;
    int selected_group_ = 0;
    std::string status_ = "Use a named color or #RRGGBB.";
    bool status_is_error_ = false;

    ftxui::Component interface_tab_;
    ftxui::Component editor_tab_;
    ftxui::Component modal_tab_;
    ftxui::Component buttons_tab_;
    ftxui::Component tabs_;
    ftxui::Component groups_;
    ftxui::Component container_;
};

class ThemeColorsModal {
public:
    using ApplyCallback = ThemeColorsModalContent::ApplyCallback;
    using ResetCallback = ThemeColorsModalContent::ResetCallback;
    using CloseCallback = std::function<void()>;

    ThemeColorsModal(
        const Theme* theme,
        ApplyCallback apply_callback,
        ResetCallback reset_callback,
        CloseCallback close_callback);

    ftxui::Component View() const { return modal_; }
    void Open();
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }

private:
    void RequestClose();

    bool open_ = false;
    CloseCallback close_callback_;
    std::shared_ptr<ThemeColorsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
