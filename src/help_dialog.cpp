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
    title_ = "Help";
    lines_ = LoadTextFileLines(path);
    help_scroll_y_ = 0;
    center_content_ = false;
}

void HelpDialog::OpenContent(const std::string& title,
                             const std::vector<std::string>& lines,
                             bool center_content) {
    open_ = true;
    title_ = title;
    lines_ = lines;
    help_scroll_y_ = 0;
    center_content_ = center_content;
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
        Element line = text(lines_[index]) | color(theme.modal_text_color);
        if (center_content_) {
            line = line | center;
        }
        help_content.push_back(line);
    }
    while (help_content.size() < visible_lines) {
        help_content.push_back(text(""));
    }

    Elements dialog_rows;
    if (!title_.empty()) {
        dialog_rows.push_back(text(title_) | bold | color(theme.modal_accent) | center);
        dialog_rows.push_back(separator() | color(theme.modal_border));
    }
    dialog_rows.push_back(
        vbox(std::move(help_content)) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_text_color) |
        frame |
        color(theme.modal_border) |
        size(HEIGHT, EQUAL, static_cast<int>(visible_lines + 2)));
    dialog_rows.push_back(separator() | color(theme.modal_border));
    dialog_rows.push_back(text("Press Escape to close.") | dim | color(theme.modal_text_color));

    return vbox(std::move(dialog_rows)) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color) |
        border |
        color(theme.modal_border) |
        size(WIDTH, GREATER_THAN, 56);
}

} // namespace textlt
