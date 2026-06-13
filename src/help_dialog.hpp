#pragma once

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "theme.hpp"

namespace textlt {

class HelpDialog {
public:
    explicit HelpDialog(const Theme* theme);

    ftxui::Component View() const;
    void Open(const std::string& path);
    void Close();
    bool IsOpen() const;

private:
    ftxui::Element Render();

    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::vector<std::string> lines_;
    ftxui::Component renderer_;
};

} // namespace textlt
