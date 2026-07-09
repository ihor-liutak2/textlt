#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "app_command_registry.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "shortcut_key.hpp"
#include "shortcut_registry.hpp"
#include "theme.hpp"

namespace textlt {

class KeyboardShortcutsModalContent : public IModalContent {
public:
    using SaveCallback = std::function<bool(std::string& error)>;
    using CloseCallback = std::function<void()>;

    KeyboardShortcutsModalContent(
        const Theme* theme,
        ShortcutRegistry* shortcut_registry,
        const AppCommandRegistry* command_registry,
        SaveCallback save_callback,
        CloseCallback close_callback);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Keyboard Shortcuts"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {108, 32}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();
    bool HandleEvent(ftxui::Event event);

private:
    ftxui::Component MakeTextButton(std::string label, std::function<void()> on_click);
    ShortcutContext CurrentContext() const;
    std::vector<ShortcutBindingView> CurrentBindings() const;
    void RebuildActionList();
    void RebuildKeyList();
    void SyncSelectionFromBinding();
    std::optional<ShortcutBindingView> SelectedBinding() const;
    std::string SelectedShortcutString() const;
    bool ApplySelectedShortcut();
    void ResetSelectedShortcut();
    void ResetAllShortcuts();
    void SetTab(int tab_index);
    void MoveSelection(int delta);
    void EnsureSelectionVisible();
    bool RecordCapturedShortcutEvent(const ftxui::Event& event);
    std::string BindingStateLabel(const ShortcutBindingView& binding) const;
    ftxui::Element RenderTabs() const;
    ftxui::Element RenderActionList() const;
    ftxui::Element RenderPicker() const;
    ftxui::Element RenderHelpLine() const;

    const Theme* theme_ = nullptr;
    ShortcutRegistry* shortcut_registry_ = nullptr;
    const AppCommandRegistry* command_registry_ = nullptr;
    SaveCallback save_callback_;
    CloseCallback close_callback_;

    int tab_index_ = 0;
    int selected_action_ = 0;
    int action_top_row_ = 0;
    int selected_modifier_ = 0;
    int selected_key_ = 0;
    std::vector<ShortcutBindingView> bindings_;
    std::vector<std::string> action_labels_;
    std::vector<std::string> modifier_labels_;
    std::vector<std::string> key_labels_;
    std::unordered_set<std::string> captured_shortcuts_;
    std::string status_ = "Choose a command, modifier and key. Terminal-reserved shortcuts are hidden.";
    bool status_is_error_ = false;

    // The command list and tabs use custom rendering, so retain their rendered
    // bounds for mouse hit testing.
    mutable ftxui::Box menu_tab_box_;
    mutable ftxui::Box text_tab_box_;
    mutable std::vector<ftxui::Box> action_row_boxes_;

    ftxui::Component menu_tab_button_;
    ftxui::Component text_tab_button_;
    ftxui::Component action_menu_;
    ftxui::Component modifier_menu_;
    ftxui::Component key_menu_;
    ftxui::Component apply_button_;
    ftxui::Component reset_button_;
    ftxui::Component reset_all_button_;
    ftxui::Component close_button_;
    ftxui::Component picker_container_;
    ftxui::Component button_container_;
    ftxui::Component container_;

    static constexpr int kVisibleActionRows = 14;
};

class KeyboardShortcutsModal {
public:
    KeyboardShortcutsModal(
        const Theme* theme,
        ShortcutRegistry* shortcut_registry,
        const AppCommandRegistry* command_registry,
        KeyboardShortcutsModalContent::SaveCallback save_callback);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    ShortcutRegistry* shortcut_registry_ = nullptr;
    const AppCommandRegistry* command_registry_ = nullptr;
    KeyboardShortcutsModalContent::SaveCallback save_callback_;
    std::shared_ptr<KeyboardShortcutsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
