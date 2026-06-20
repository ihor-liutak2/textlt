#include "path_operation_dialog.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

constexpr size_t kMaximumMatches = 3;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string TrimTrailingNewlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

} // namespace

PathOperationContent::PathOperationContent(const Theme* theme,
                                           std::string* from,
                                           std::string* to,
                                           std::string* error,
                                           ConfirmAction on_confirm)
    : theme_(theme),
      from_(from),
      to_(to),
      error_(error),
      on_confirm_(std::move(on_confirm)) {
    Configure("Path", {}, 0, 0);
}

void PathOperationContent::Configure(std::string title,
                                     std::vector<std::string> candidates,
                                     size_t from_cursor_position,
                                     size_t to_cursor_position) {
    title_ = std::move(title);
    candidates_ = std::move(candidates);
    selected_match_ = 0;
    focused_field_ = 0;
    to_touched_ = false;
    UpdateMatches();

    ftxui::InputOption from_option;
    from_option.multiline = false;
    from_option.cursor_position = static_cast<int>(from_cursor_position);
    from_option.on_change = [this] {
        selected_match_ = 0;
        UpdateMatches();
        if (!to_touched_ && from_ && to_) {
            *to_ = *from_;
        }
    };
    from_option.on_enter = [this] {
        AcceptSelectedCandidate();
        focused_field_ = 1;
        if (to_input_) {
            to_input_->TakeFocus();
        }
    };
    from_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return state.element |
            ftxui::bgcolor(theme.modal_input_bg) |
            ftxui::color(theme.modal_input_fg);
    };

    ftxui::InputOption to_option;
    to_option.multiline = false;
    to_option.cursor_position = static_cast<int>(to_cursor_position);
    to_option.on_change = [this] {
        to_touched_ = true;
    };
    to_option.on_enter = [this] {
        if (on_confirm_) {
            on_confirm_();
        }
    };
    to_option.transform = from_option.transform;

    from_input_ = ftxui::Input(from_, "source path", from_option);
    to_input_ = ftxui::Input(to_, "destination path", to_option);
    container_ = ftxui::Container::Vertical({from_input_, to_input_});
    component_ = ftxui::CatchEvent(container_, [this](ftxui::Event event) {
        return HandleEvent(event);
    });
}

void PathOperationContent::Rebuild() {
    if (from_input_ && to_input_) {
        container_ = ftxui::Container::Vertical({from_input_, to_input_});
        component_ = ftxui::CatchEvent(container_, [this](ftxui::Event event) {
            return HandleEvent(event);
        });
    }
}

void PathOperationContent::UpdateMatches() {
    matches_.clear();
    const std::string query = from_ ? Lowercase(*from_) : "";
    for (const std::string& candidate : candidates_) {
        if (query.empty() || Lowercase(candidate).find(query) != std::string::npos) {
            matches_.push_back(candidate);
            if (matches_.size() == kMaximumMatches) {
                break;
            }
        }
    }
    if (selected_match_ >= static_cast<int>(matches_.size())) {
        selected_match_ = matches_.empty() ? 0 : static_cast<int>(matches_.size()) - 1;
    }
}

void PathOperationContent::AcceptSelectedCandidate() {
    if (!matches_.empty() &&
        selected_match_ >= 0 &&
        selected_match_ < static_cast<int>(matches_.size()) &&
        from_) {
        *from_ = matches_[selected_match_];
    }
    if (from_ && to_) {
        *to_ = *from_;
    }
}

bool PathOperationContent::HandleEvent(ftxui::Event event) {
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
        for (size_t index = 0; index < match_boxes_.size(); ++index) {
            if (match_boxes_[index].Contain(event.mouse().x, event.mouse().y)) {
                selected_match_ = static_cast<int>(index);
                AcceptSelectedCandidate();
                focused_field_ = 1;
                if (to_input_) {
                    to_input_->TakeFocus();
                }
                return true;
            }
        }
    }

    if (event == ftxui::Event::Tab) {
        if (focused_field_ == 0) {
            AcceptSelectedCandidate();
            focused_field_ = 1;
            if (to_input_) {
                to_input_->TakeFocus();
            }
            return true;
        }
        focused_field_ = 0;
        if (from_input_) {
            from_input_->TakeFocus();
        }
        return true;
    }

    if (event == ftxui::Event::TabReverse) {
        focused_field_ = 0;
        if (from_input_) {
            from_input_->TakeFocus();
        }
        return true;
    }

    if (focused_field_ == 0 && event == ftxui::Event::ArrowDown && !matches_.empty()) {
        selected_match_ = (selected_match_ + 1) % static_cast<int>(matches_.size());
        return true;
    }

    if (focused_field_ == 0 && event == ftxui::Event::ArrowUp && !matches_.empty()) {
        selected_match_ =
            (selected_match_ + static_cast<int>(matches_.size()) - 1) %
            static_cast<int>(matches_.size());
        return true;
    }

    return false;
}

void PathOperationContent::TakeFocus() {
    focused_field_ = 0;
    if (from_input_) {
        from_input_->TakeFocus();
    }
}

ftxui::Element PathOperationContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    match_boxes_.assign(matches_.size(), {});
    Elements match_rows;
    for (size_t index = 0; index < matches_.size(); ++index) {
        Element row = text("  " + matches_[index]) |
            size(WIDTH, EQUAL, 68) |
            reflect(match_boxes_[index]);
        if (static_cast<int>(index) == selected_match_) {
            row = row |
                bgcolor(theme.modal_selected_item_bg) |
                color(theme.modal_selected_item_fg);
        } else {
            row = row | color(theme.modal_text_color);
        }
        match_rows.push_back(row);
    }
    while (match_rows.size() < kMaximumMatches) {
        match_rows.push_back(text(""));
    }

    Element error_line = text("");
    if (error_ && !error_->empty()) {
        error_line = text(" Error: " + *error_) | color(Color::Red);
    }

    return vbox({
        hbox({
            text(" From: ") | color(theme.modal_text_color),
            from_input_->Render() | xflex | bgcolor(theme.modal_input_bg),
        }),
        vbox(std::move(match_rows)),
        separator() | color(theme.modal_border),
        hbox({
            text(" To:   ") | color(theme.modal_text_color),
            to_input_->Render() | xflex | bgcolor(theme.modal_input_bg),
        }),
        error_line,
    }) |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_text_color);
}

PathOperationDialog::PathOperationDialog(const Theme* theme, ConfirmCallback on_confirm)
    : on_confirm_(std::move(on_confirm)),
      theme_(theme) {
    content_impl_ = std::make_shared<PathOperationContent>(
        theme_,
        &from_,
        &to_,
        &error_,
        [this] { Confirm(); });
    modal_window_ = std::make_shared<ModalWindow>(
        content_impl_,
        theme_,
        [this] { Close(); });
    RebuildModal();
}

void PathOperationDialog::RebuildModal() {
    if (!modal_window_) {
        return;
    }

    modal_window_->SetTheme(theme_);
    modal_window_->SetBodyFrameScrolling(false);
    modal_window_->SetModalSize(78, 12);
    modal_window_->SetFooterText("Tab accepts source, Enter confirms, Escape cancels.");
    modal_window_->SetFooterButtons({
        {"Confirm", [this] { Confirm(); }},
        {"Cancel", [this] { Close(); }},
    });
}

ftxui::Component PathOperationDialog::View() const {
    return modal_window_;
}

void PathOperationDialog::Open(PathOperationMode mode,
                               std::string initial_from,
                               std::vector<std::string> candidates) {
    mode_ = mode;
    error_.clear();
    from_ = std::move(initial_from);
    to_ = from_;

    content_impl_->SetTheme(theme_);
    content_impl_->Configure(
        mode == PathOperationMode::Rename ? "Rename" : "Move",
        std::move(candidates),
        from_.size(),
        to_.size());
    RebuildModal();
    modal_window_->RefreshChildren();
    TakeFocus();
}

void PathOperationDialog::Close() {
    mode_ = PathOperationMode::None;
    error_.clear();
}

bool PathOperationDialog::IsOpen() const {
    return mode_ != PathOperationMode::None;
}

void PathOperationDialog::TakeFocus() {
    if (content_impl_) {
        content_impl_->TakeFocus();
    }
}

void PathOperationDialog::Confirm() {
    from_ = TrimTrailingNewlines(from_);
    to_ = TrimTrailingNewlines(to_);

    if (from_.empty()) {
        error_ = "Enter a source path.";
        return;
    }
    if (to_.empty()) {
        error_ = "Enter a destination path.";
        return;
    }

    std::string callback_error;
    if (on_confirm_ && on_confirm_(mode_, from_, to_, callback_error)) {
        Close();
        return;
    }
    error_ = callback_error.empty() ? "Path operation failed." : callback_error;
}

} // namespace textlt
