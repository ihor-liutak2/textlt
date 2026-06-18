#pragma once

#include <functional>
#include <string>
#include <vector>
#include <memory> // For shared_ptr

#include "ftxui/component/component.hpp"
#include "theme.hpp"
#include "modal_interface.hpp" // New include
#include "modal_window.hpp"    // New include

namespace textlt {

// New class: ThemeSelectionContent implements IModalContent
class ThemeSelectionContent : public IModalContent {
public:
    using ThemeCallback = std::function<void(const std::string& theme_name)>;

    ThemeSelectionContent(const Theme* active_theme, ThemeCallback on_preview, ThemeCallback on_select);

    // IModalContent overrides
    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return menu_; }
    std::string GetTitle() override { return "Select Theme"; }

    // Public methods for ThemeDialog to call
    void SetThemes(const std::vector<Theme>& themes, const std::string& active_name);
    void SetTheme(const Theme* new_theme) { active_theme_ = new_theme; } // New method
    void TakeFocus(); // Delegate to internal menu
    
private:
    void SelectCurrentTheme();

    const Theme* active_theme_ = nullptr; // This can be updated by SetTheme
    ThemeCallback on_preview_;
    ThemeCallback on_select_;
    std::vector<std::string> theme_names_;
    int selected_theme_ = 0;
    ftxui::Component menu_;
};


// Existing ThemeDialog class, now wrapping ModalWindow and ThemeSelectionContent
class ThemeDialog {
public:
    // Keep existing callbacks for external usage
    using ThemeCallback = std::function<void(const std::string& theme_name)>;

    // Constructor will now build ThemeSelectionContent and ModalWindow
    ThemeDialog(const Theme* active_theme, ThemeCallback on_preview, ThemeCallback on_select);

    // External interface remains the same for app_view.cpp
    ftxui::Component View() const;
    void Open(const std::vector<Theme>& themes, const std::string& active_name);
    void Close();
    bool IsOpen() const;
    void TakeFocus();
    void SetTheme(const Theme* new_theme); // New method to propagate theme changes

private:
    // Internal state and components
    bool open_ = false; // Manages if the modal is currently open
    const Theme* current_active_theme_ = nullptr; // Keep track of the current theme
    std::shared_ptr<ThemeSelectionContent> content_impl_;
    std::shared_ptr<ModalWindow> modal_window_; // This is the component that will be rendered and handle events
};

} // namespace textlt
