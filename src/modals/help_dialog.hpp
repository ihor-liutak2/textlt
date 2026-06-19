#pragma once

#include <string>
#include <vector>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "theme.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"

namespace textlt {

class HelpDialogContent : public IModalContent {
public:
    explicit HelpDialogContent(const Theme* theme);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return renderer_; }
    std::string GetTitle() override;

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void SetContent(const std::string& title,
                    const std::vector<std::string>& lines,
                    bool center_content,
                    size_t visible_rows,
                    int content_width);
    void ScrollBy(int delta);
    bool OnEvent(ftxui::Event event);

private:
    size_t MaxScrollY() const;
    void ClampScroll();
    ftxui::Element RenderScrollbar(size_t visible_rows, const Theme& theme) const;

    const Theme* theme_ = nullptr;
    std::string title_ = "Help";
    std::vector<std::string> lines_;
    size_t scroll_y_ = 0;
    size_t visible_rows_ = 22;
    int content_width_ = 76;
    bool center_content_ = false;
    ftxui::Component renderer_;
};

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
    void OpenWithContent(const std::string& title,
                         const std::vector<std::string>& lines,
                         bool center_content);

    bool open_ = false;
    const Theme* theme_ = nullptr;
    std::shared_ptr<HelpDialogContent> content_impl_;
    std::shared_ptr<ModalWindow> modal_window_;
};

} // namespace textlt
