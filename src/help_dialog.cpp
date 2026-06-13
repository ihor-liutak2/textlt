#include "help_dialog.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "file_utils.hpp"

namespace textlt {

namespace {

constexpr size_t kHelpVisibleRows = 22;

} // namespace

HelpDialog::HelpDialog(const Theme* theme)
    : theme_(theme) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
}

ftxui::Component HelpDialog::View() const {
    return renderer_;
}

void HelpDialog::Open(const std::string& path) {
    open_ = true;
    lines_ = LoadTextFileLines(path);
    help_scroll_y_ = 0;
}

void HelpDialog::Close() {
    open_ = false;
}

bool HelpDialog::IsOpen() const {
    return open_;
}

bool HelpDialog::OnEvent(ftxui::Event event) {
    if (!open_) {
        return false;
    }

    if (event == ftxui::Event::ArrowDown) {
        help_scroll_y_ = std::min(help_scroll_y_ + 1, MaxScrollY());
        return true;
    }

    if (event == ftxui::Event::ArrowUp) {
        help_scroll_y_ = help_scroll_y_ > 0 ? help_scroll_y_ - 1 : 0;
        return true;
    }

    if (event.is_mouse()) {
        auto mouse = event.mouse();
        if (mouse.button == ftxui::Mouse::WheelDown) {
            help_scroll_y_ = std::min(help_scroll_y_ + 2, MaxScrollY());
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelUp) {
            help_scroll_y_ = help_scroll_y_ > 2 ? help_scroll_y_ - 2 : 0;
            return true;
        }
    }

    return false;
}

size_t HelpDialog::VisibleLineCount() const {
    return kHelpVisibleRows;
}

size_t HelpDialog::MaxScrollY() const {
    const size_t visible_lines = VisibleLineCount();
    return lines_.size() > visible_lines ? lines_.size() - visible_lines : 0;
}

void HelpDialog::ClampScroll() {
    help_scroll_y_ = std::min(help_scroll_y_, MaxScrollY());
}

ftxui::Element HelpDialog::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ClampScroll();

    Elements help_content;
    const size_t visible_lines = VisibleLineCount();
    const size_t end = std::min(lines_.size(), help_scroll_y_ + visible_lines);
    for (size_t index = help_scroll_y_; index < end; ++index) {
        help_content.push_back(text(lines_[index]) | color(theme.modal_text_color));
    }
    while (help_content.size() < visible_lines) {
        help_content.push_back(text(""));
    }

    return vbox({
        text("Help") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        vbox(std::move(help_content)) |
            bgcolor(theme.modal_input_bg) |
            color(theme.modal_text_color) |
            frame |
            color(theme.modal_border) |
            size(HEIGHT, EQUAL, static_cast<int>(visible_lines + 2)),
        separator() | color(theme.modal_border),
        text("Press Escape to close.") | dim | color(theme.modal_text_color),
    }) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color) |
        border |
        color(theme.modal_border) |
        size(WIDTH, GREATER_THAN, 56);
}

} // namespace textlt
