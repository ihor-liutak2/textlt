#include "modal_window.hpp"

#include <algorithm>
#include <utility>

namespace textlt {

namespace {

constexpr size_t kMaximumFooterButtons = 3;

} // namespace

ModalWindow::ModalWindow(std::shared_ptr<IModalContent> content,
                         const Theme* theme,
                         OnCloseCallback on_close)
    : content_(std::move(content)),
      theme_(theme),
      on_close_(std::move(on_close)) {
    header_close_button_ = ftxui::Button(MakeTextButtonOption("■", [this] {
        if (on_close_) {
            on_close_();
        }
    }));

    RebuildChildren();
}

ftxui::ButtonOption ModalWindow::MakeTextButtonOption(const std::string& label,
                                                      ButtonCallback callback) const {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = label;
    option.on_click = std::move(callback);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element button = ftxui::text("[" + state.label + "]");
        if (state.focused || state.active) {
            return button |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return button | ftxui::color(theme.modal_accent);
    };
    return option;
}

void ModalWindow::SetFooterText(std::string text) {
    footer_text_ = std::move(text);
}

void ModalWindow::SetBodyWidth(int columns) {
    modal_width_ = std::max(20, columns + 2);
    modal_size_overridden_ = true;
}

void ModalWindow::SetModalSize(int columns, int rows) {
    modal_width_ = std::max(20, columns);
    modal_height_ = std::max(0, rows);
    modal_size_overridden_ = true;
}

void ModalWindow::RebuildChildren() {
    DetachAllChildren();
    Add(content_->GetMainComponent());
    Add(header_close_button_);
    for (const auto& button : footer_buttons_) {
        Add(button);
    }
    if (active_child_index_ >= ChildCount()) {
        active_child_index_ = 0;
    }
}

void ModalWindow::SetFooterButtons(std::vector<FooterButton> buttons) {
    footer_buttons_.clear();
    const size_t count = std::min(buttons.size(), kMaximumFooterButtons);
    footer_buttons_.reserve(count);

    for (size_t index = 0; index < count; ++index) {
        auto callback = std::move(buttons[index].on_click);
        footer_buttons_.push_back(ftxui::Button(MakeTextButtonOption(buttons[index].label, [callback = std::move(callback)] {
            if (callback) {
                callback();
            }
        })));
    }
    RebuildChildren();
}

ftxui::Component ModalWindow::ActiveChild() {
    if (children_.empty()) {
        return nullptr;
    }
    active_child_index_ = std::min(active_child_index_, children_.size() - 1);
    return children_[active_child_index_];
}

void ModalWindow::SetActiveChild(ftxui::ComponentBase* child) {
    for (size_t index = 0; index < children_.size(); ++index) {
        if (children_[index].get() == child) {
            active_child_index_ = index;
            return;
        }
    }
}

void ModalWindow::MoveActionFocus(int delta) {
    if (children_.size() <= 1) {
        return;
    }

    const size_t first_action = 1;
    const size_t action_count = children_.size() - first_action;
    if (action_count == 0) {
        return;
    }

    const long long current_action = active_child_index_ < first_action
        ? -1
        : static_cast<long long>(active_child_index_ - first_action);
    const long long next =
        current_action + static_cast<long long>(delta);
    active_child_index_ =
        first_action + static_cast<size_t>(
            (next % static_cast<long long>(action_count) +
             static_cast<long long>(action_count)) %
            static_cast<long long>(action_count));
}

ftxui::Element ModalWindow::RenderHeader(const Theme& theme) {
    using namespace ftxui;

    Elements row = {
        text(" " + content_->GetTitle()) |
            bold |
            color(theme.modal_accent) |
            size(WIDTH, EQUAL, std::max(1, BodyWidth() - 6)),
    };

    if (show_header_close_) {
        row.push_back(filler());
        row.push_back(header_close_button_->Render());
    }

    return hbox(std::move(row)) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color);
}

ftxui::Element ModalWindow::RenderBody(const Theme& theme) {
    using namespace ftxui;

    Element body = content_->Render() |
        size(WIDTH, EQUAL, BodyWidth()) |
        size(HEIGHT, EQUAL, BodyHeight());

    if (body_frame_scrolling_) {
        body = body |
            vscroll_indicator |
            yframe;
    }

    return body |
        reflect(body_box_) |
        size(WIDTH, EQUAL, BodyWidth()) |
        size(HEIGHT, EQUAL, BodyHeight()) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

ftxui::Element ModalWindow::RenderFooter(const Theme& theme) {
    using namespace ftxui;

    Elements row;
    const std::string footer_text =
        content_ && !content_->GetFooterText().empty()
            ? content_->GetFooterText()
            : footer_text_;
    if (!footer_text.empty()) {
        row.push_back(text(" " + footer_text + " ") |
                      dim |
                      color(theme.modal_text_color));
    }

    row.push_back(filler());

    for (size_t index = 0; index < footer_buttons_.size(); ++index) {
        if (index > 0) {
            row.push_back(text(" "));
        }
        row.push_back(footer_buttons_[index]->Render());
    }

    if (footer_text.empty() && footer_buttons_.empty()) {
        row.push_back(text(" "));
    }

    return hbox(std::move(row)) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color) |
        size(WIDTH, EQUAL, BodyWidth());
}

int ModalWindow::ModalWidth() const {
    if (!modal_size_overridden_ && content_) {
        const ModalSizePreference preference = content_->GetModalSizePreference();
        if (preference.width > 0) {
            return std::max(20, preference.width);
        }
    }
    return modal_width_;
}

int ModalWindow::ModalHeight() const {
    if (!modal_size_overridden_ && content_) {
        const ModalSizePreference preference = content_->GetModalSizePreference();
        if (preference.height > 0) {
            return std::max(0, preference.height);
        }
    }
    return modal_height_;
}

int ModalWindow::BodyWidth() const {
    return std::max(1, ModalWidth() - 2);
}

int ModalWindow::BodyHeight() const {
    const int modal_height = ModalHeight();
    if (modal_height > 0) {
        int used_rows = 2;
        if (show_header_ && FrameStyle() == ModalFrameStyle::HeaderInsideBorder) {
            used_rows += 2;
        }
        if (show_footer_) {
            used_rows += 2;
        }
        return std::max(1, modal_height - used_rows);
    }
    return max_body_height_;
}

ModalFrameStyle ModalWindow::FrameStyle() const {
    return content_ ? content_->GetModalFrameStyle() : ModalFrameStyle::HeaderInsideBorder;
}

ftxui::Element ModalWindow::Render() {
    using namespace ftxui;
    const Theme& current_theme = theme_ ? *theme_ : FallbackTheme();
    const ModalFrameStyle frame_style = FrameStyle();

    Elements rows;
    if (show_header_ && frame_style == ModalFrameStyle::HeaderInsideBorder) {
        rows.push_back(RenderHeader(current_theme));
        rows.push_back(separator() | color(current_theme.modal_border));
    }

    rows.push_back(RenderBody(current_theme));

    if (show_footer_) {
        rows.push_back(separator() | color(current_theme.modal_border));
        rows.push_back(RenderFooter(current_theme));
    }

    Element framed = vbox(std::move(rows));
    if (frame_style == ModalFrameStyle::TitleInBorder) {
        Element title = content_ ? content_->RenderTitle() : text("");
        if (show_header_close_) {
            title = hbox({
                title,
                filler(),
                header_close_button_->Render(),
            }) | size(WIDTH, EQUAL, BodyWidth());
        }
        framed = window(
            title,
            framed);
    } else {
        framed = framed | border;
    }

    return framed |
        bgcolor(current_theme.modal_background) |
        color(current_theme.modal_border) |
        clear_under |
        size(WIDTH, EQUAL, ModalWidth()) |
        (ModalHeight() > 0
            ? size(HEIGHT, EQUAL, ModalHeight())
            : size(HEIGHT, LESS_THAN, max_body_height_ + 6));
}

bool ModalWindow::OnEvent(ftxui::Event event) {
    if (event.is_mouse()) {
        if (ComponentBase::OnEvent(event)) {
            return true;
        }
        return true;
    }

    if ((event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) &&
        active_child_index_ == 0) {
        if (ftxui::Component active = ActiveChild()) {
            active->OnEvent(event);
        }
        return true;
    }

    if ((event == ftxui::Event::Tab || event == ftxui::Event::TabReverse) &&
        active_child_index_ == 0) {
        if (ftxui::Component active = ActiveChild()) {
            if (active->OnEvent(event)) {
                return true;
            }
        }
    }

    if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::TabReverse) {
        MoveActionFocus(-1);
        return true;
    }

    if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Tab) {
        MoveActionFocus(1);
        return true;
    }

    if (ftxui::Component active = ActiveChild()) {
        if (active->OnEvent(event)) {
            return true;
        }
    }

    if (event == ftxui::Event::Escape) {
        if (on_close_) {
            on_close_();
        }
        return true;
    }

    return true;
}

} // namespace textlt
