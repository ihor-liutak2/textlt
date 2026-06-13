#include "help_dialog.hpp"

#include <utility>

#include "ftxui/dom/elements.hpp"
#include "file_utils.hpp"

namespace textlt {

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
}

void HelpDialog::Close() {
    open_ = false;
}

bool HelpDialog::IsOpen() const {
    return open_;
}

ftxui::Element HelpDialog::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    Elements help_content;
    for (const std::string& line : lines_) {
        help_content.push_back(text(line) | color(theme.modal_text_color));
    }

    return vbox({
        text("Help") | bold | color(theme.modal_accent),
        separator() | color(theme.modal_border),
        vbox(std::move(help_content)) |
            bgcolor(theme.modal_input_bg) |
            color(theme.modal_text_color) |
            frame |
            color(theme.modal_border) |
            size(HEIGHT, LESS_THAN, 18),
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
