#include "help_dialog.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "file_utils.hpp"

namespace textlt {

namespace {

constexpr int kHelpModalWidth = 80;
constexpr int kHelpModalHeight = 28;
constexpr int kHelpBodyWidth = kHelpModalWidth - 2;
constexpr size_t kHelpVisibleRows = 22;

constexpr int kAboutModalWidth = 60;
constexpr int kAboutModalHeight = 14;
constexpr int kAboutBodyWidth = kAboutModalWidth - 2;
constexpr size_t kAboutVisibleRows = 8;

} // namespace

HelpDialogContent::HelpDialogContent(const Theme* theme)
    : theme_(theme) {
    renderer_ = ftxui::Renderer([this] { return Render(); });
    renderer_ = ftxui::CatchEvent(renderer_, [this](ftxui::Event event) {
        return OnEvent(event);
    });
}

void HelpDialogContent::SetContent(const std::string& title,
                                   const std::vector<std::string>& lines,
                                   bool center_content,
                                   size_t visible_rows,
                                   int content_width) {
    title_ = title.empty() ? "Help" : title;
    lines_ = lines;
    center_content_ = center_content;
    visible_rows_ = std::max<size_t>(1, visible_rows);
    content_width_ = std::max(1, content_width);
    scroll_y_ = 0;
    ClampScroll();
}

std::string HelpDialogContent::GetTitle() {
    return title_.empty() ? "Help" : title_;
}

size_t HelpDialogContent::MaxScrollY() const {
    return lines_.size() > visible_rows_ ? lines_.size() - visible_rows_ : 0;
}

void HelpDialogContent::ClampScroll() {
    scroll_y_ = std::min(scroll_y_, MaxScrollY());
}

void HelpDialogContent::ScrollBy(int delta) {
    const long long next =
        static_cast<long long>(scroll_y_) + static_cast<long long>(delta);
    scroll_y_ = static_cast<size_t>(
        std::clamp<long long>(next, 0, static_cast<long long>(MaxScrollY())));
}

bool HelpDialogContent::OnEvent(ftxui::Event event) {
    if (event == ftxui::Event::ArrowDown) {
        ScrollBy(1);
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        ScrollBy(-1);
        return true;
    }
    if (event == ftxui::Event::PageDown) {
        ScrollBy(static_cast<int>(visible_rows_));
        return true;
    }
    if (event == ftxui::Event::PageUp) {
        ScrollBy(-static_cast<int>(visible_rows_));
        return true;
    }
    if (event == ftxui::Event::Home) {
        scroll_y_ = 0;
        return true;
    }
    if (event == ftxui::Event::End) {
        scroll_y_ = MaxScrollY();
        return true;
    }

    if (event.is_mouse()) {
        const auto mouse = event.mouse();
        if (mouse.button == ftxui::Mouse::WheelDown) {
            ScrollBy(3);
            return true;
        }
        if (mouse.button == ftxui::Mouse::WheelUp) {
            ScrollBy(-3);
            return true;
        }
    }

    return false;
}

ftxui::Element HelpDialogContent::RenderScrollbar(size_t visible_rows,
                                                  const Theme& theme) const {
    using namespace ftxui;

    Elements cells;
    cells.reserve(visible_rows);

    const size_t total_rows = std::max(lines_.size(), visible_rows);
    const bool needs_scrollbar = lines_.size() > visible_rows;
    const size_t thumb_height = needs_scrollbar
        ? std::max<size_t>(1, (visible_rows * visible_rows) / total_rows)
        : visible_rows;
    const size_t track_space = visible_rows > thumb_height ? visible_rows - thumb_height : 0;
    const size_t thumb_top = needs_scrollbar && MaxScrollY() > 0
        ? (scroll_y_ * track_space) / MaxScrollY()
        : 0;

    for (size_t row = 0; row < visible_rows; ++row) {
        const bool active = row >= thumb_top && row < thumb_top + thumb_height;
        cells.push_back(text(active ? "█" : "│") |
                        color(active ? theme.modal_accent : theme.modal_border));
    }

    return vbox(std::move(cells));
}

ftxui::Element HelpDialogContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    ClampScroll();

    Elements rows;
    rows.reserve(visible_rows_);

    const size_t end = std::min(lines_.size(), scroll_y_ + visible_rows_);
    for (size_t index = scroll_y_; index < end; ++index) {
        Element line = text(lines_[index]) | color(theme.modal_text_color);
        if (center_content_) {
            line = line | center;
        }
        rows.push_back(line | size(WIDTH, EQUAL, content_width_ - 1));
    }

    while (rows.size() < visible_rows_) {
        rows.push_back(text("") | size(WIDTH, EQUAL, content_width_ - 1));
    }

    return hbox({
        vbox(std::move(rows)) |
            size(WIDTH, EQUAL, content_width_ - 1) |
            size(HEIGHT, EQUAL, static_cast<int>(visible_rows_)),
        RenderScrollbar(visible_rows_, theme),
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_text_color) |
        size(WIDTH, EQUAL, content_width_) |
        size(HEIGHT, EQUAL, static_cast<int>(visible_rows_));
}

HelpDialog::HelpDialog(const Theme* theme)
    : theme_(theme) {
    content_impl_ = std::make_shared<HelpDialogContent>(theme_);
    modal_window_ = std::make_shared<ModalWindow>(
        content_impl_,
        theme_,
        [this] { Close(); });
    modal_window_->SetBodyFrameScrolling(false);
    modal_window_->SetFooterText("Arrow keys and mouse wheel scroll, Escape closes.");
    modal_window_->SetFooterButtons({
        {"Close", [this] { Close(); }},
    });
}

ftxui::Component HelpDialog::View() const {
    return modal_window_;
}

void HelpDialog::Open(const std::string& path) {
    OpenWithContent("Help", LoadTextFileLines(path), false);
}

void HelpDialog::OpenContent(const std::string& title,
                             const std::vector<std::string>& lines,
                             bool center_content) {
    OpenWithContent(title, lines, center_content);
}

void HelpDialog::OpenWithContent(const std::string& title,
                                 const std::vector<std::string>& lines,
                                 bool center_content) {
    open_ = true;
    content_impl_->SetTheme(theme_);

    if (center_content) {
        modal_window_->SetModalSize(kAboutModalWidth, kAboutModalHeight);
        modal_window_->SetFooterText("Escape closes.");
        content_impl_->SetContent(title, lines, true, kAboutVisibleRows, kAboutBodyWidth);
    } else {
        modal_window_->SetModalSize(kHelpModalWidth, kHelpModalHeight);
        modal_window_->SetFooterText("Arrow keys and mouse wheel scroll, Escape closes.");
        content_impl_->SetContent(title, lines, false, kHelpVisibleRows, kHelpBodyWidth);
    }

    content_impl_->GetMainComponent()->TakeFocus();
}

void HelpDialog::Close() {
    open_ = false;
}

bool HelpDialog::IsOpen() const {
    return open_;
}

bool HelpDialog::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_window_) {
        return false;
    }
    return modal_window_->OnEvent(event);
}

} // namespace textlt
