#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "theme.hpp"

namespace textlt {

class ThemeDialog {
public:
    using SelectCallback = std::function<void(const std::string& theme_name)>;

    ThemeDialog(const Theme* active_theme, SelectCallback on_select);

    ftxui::Component View() const;
    void Open(const std::vector<Theme>& themes, const std::string& active_name);
    void Close();
    bool IsOpen() const;
    void TakeFocus();

private:
    void SelectCurrentTheme();
    ftxui::Element Render();

    const Theme* active_theme_ = nullptr;
    SelectCallback on_select_;
    bool open_ = false;
    std::vector<std::string> theme_names_;
    int selected_theme_ = 0;
    ftxui::Component menu_;
    ftxui::Component renderer_;
};

} // namespace textlt
