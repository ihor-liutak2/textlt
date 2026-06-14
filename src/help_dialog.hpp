#pragma once

#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "theme.hpp"

namespace textlt {

class HelpDialog {
public:
    explicit HelpDialog(const Theme* theme);

    ftxui::Component View() const;
    void Open(const std::string& path);
    void OpenContent(const std::string& title,
                     const std::vector<std::string>& lines,
                     bool center_content = false);
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    ftxui::Element Render();
    size_t VisibleLineCount() const;
    size_t MaxScrollY() const;
    void ClampScroll();

    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::string title_ = "Help";
    std::vector<std::string> lines_;
    size_t help_scroll_y_ = 0;
    bool center_content_ = false;
    ftxui::Component renderer_;
};

} // namespace textlt
