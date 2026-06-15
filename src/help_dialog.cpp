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
    if (center_content_) {
        return std::max<size_t>(1, std::min<size_t>(lines_.size(), 8));
    }
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
    while (!center_content_ && help_content.size() < visible_lines) {
        help_content.push_back(text(""));
    }

    if (center_content_) {
        Element about_content =
            vbox(std::move(help_content)) |
            bgcolor(theme.modal_background) |
            color(theme.modal_text_color) |
            size(WIDTH, EQUAL, 56);

        // About is a compact informational window, so it avoids the scrollable
        // help viewport padding that would otherwise add dead vertical space.
        return window(
            text(title_.empty() ? "About" : title_) | bold | color(theme.modal_accent),
            about_content) |
            bgcolor(theme.modal_background) |
            color(theme.modal_text_color) |
            size(WIDTH, EQUAL, 60) |
            size(HEIGHT, LESS_THAN, 14) |
            clear_under;
    }

    Element help_viewport =
        vbox(std::move(help_content)) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_text_color) |
        frame |
        size(WIDTH, EQUAL, 68) |
        size(HEIGHT, EQUAL, static_cast<int>(visible_lines));

    Element footer =
        text("Press Escape to close.") |
        dim |
        color(theme.modal_text_color);

    Element dialog_body = vbox({
        help_viewport,
        separator() | color(theme.modal_border),
        footer,
    }) | size(WIDTH, EQUAL, 68);

    return window(
        text(title_.empty() ? "Help" : title_) | bold | color(theme.modal_accent),
        dialog_body) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color) |
        color(theme.modal_border) |
        size(WIDTH, EQUAL, 72) |
        size(HEIGHT, EQUAL, static_cast<int>(visible_lines + 4)) |
        clear_under;
}

} // namespace textlt
