#pragma once

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "editor_utils.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "keyboard_shortcuts.hpp"
#include "theme.hpp"

namespace textlt {

// The displayed rows wrap, but text_ keeps its original line breaks and bytes.
class WrappedTextBox final : public ftxui::ComponentBase {
public:
    WrappedTextBox(std::string& text, const Theme& theme, int columns,
        std::string placeholder, bool editable)
        : text_(text), theme_(theme), columns_(std::max(1, columns - 1)),
          placeholder_(std::move(placeholder)), editable_(editable) {}

    bool Focusable() const override { return true; }
    bool AllSelected() const { return all_selected_; }

    void Reset() {
        cursor_ = top_ = 0;
        all_selected_ = dragging_scrollbar_ = false;
        last_click_ = {};
        rows_dirty_ = follow_cursor_ = true;
    }

    ftxui::Element OnRender() override {
        using namespace ftxui;
        UpdateRows();
        const size_t height = Height();
        const size_t cursor_row = CursorRow();
        top_ = std::min(top_, MaxTop());
        if (follow_cursor_) {
            if (cursor_row < top_) {
                top_ = cursor_row;
            } else if (cursor_row - top_ >= height) {
                top_ = cursor_row - height + 1;
            }
            follow_cursor_ = false;
        }

        Elements lines;
        const bool scrollable = MaxTop() > 0;
        const size_t thumb = scrollable ? std::max<size_t>(1, height * height / rows_.size()) : height;
        const size_t travel = height - thumb;
        const size_t thumb_start = scrollable ? top_ * travel / MaxTop() : 0;
        for (size_t i = 0; i < height; ++i) {
            const size_t index = top_ + i;
            Element line = index < rows_.size() ? RenderRow(rows_[index], index == cursor_row)
                : text("");
            if (text_.empty() && i == 0) {
                line = text(placeholder_) | dim;
            }
            if (index < rows_.size() && all_selected_ && !text_.empty()) {
                line |= bgcolor(theme_.modal_selected_item_bg) | color(theme_.modal_selected_item_fg);
            }

            Element scrollbar = text(" ");
            if (scrollable) {
                scrollbar = i >= thumb_start && i < thumb_start + thumb
                    ? text(" ") | bgcolor(theme_.modal_accent)
                    : text("│") | color(theme_.modal_border);
            }
            lines.push_back(hbox({
                line | size(WIDTH, EQUAL, columns_),
                scrollbar,
            }));
        }
        return vbox(std::move(lines)) | flex | reflect(box_) |
            bgcolor(theme_.modal_input_bg) | color(theme_.modal_input_fg);
    }

    bool OnEvent(ftxui::Event event) override {
        using namespace ftxui;
        UpdateRows();
        if (event.is_mouse()) {
            return HandleMouse(event);
        }
        if (!Focused()) {
            return false;
        }

        if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'a')) {
            SelectAll();
            return true;
        }
        if (event.is_character() || event == Event::Return ||
            event == Event::Backspace || event == Event::Delete) {
            if (!editable_) {
                return true;
            }
            const bool selected = std::exchange(all_selected_, false);
            if (selected) {
                text_.clear();
                cursor_ = 0;
            }
            if (event.is_character() || event == Event::Return) {
                const std::string value = event == Event::Return ? "\n" : event.character();
                text_.insert(cursor_, value);
                cursor_ += value.size();
            } else if (!selected && event == Event::Backspace && cursor_ > 0) {
                const size_t start = utils::PreviousUtf8CodepointStart(text_, cursor_);
                text_.erase(start, cursor_ - start);
                cursor_ = start;
            } else if (!selected && event == Event::Delete && cursor_ < text_.size()) {
                text_.erase(cursor_, utils::NextUtf8CodepointStart(text_, cursor_) - cursor_);
            }
            rows_dirty_ = follow_cursor_ = true;
            return true;
        }

        if (event == Event::ArrowLeft || event == Event::ArrowRight) {
            if (all_selected_) {
                cursor_ = event == Event::ArrowLeft ? 0 : text_.size();
            } else if (event == Event::ArrowLeft && cursor_ > 0) {
                cursor_ = utils::PreviousUtf8CodepointStart(text_, cursor_);
            } else if (event == Event::ArrowRight && cursor_ < text_.size()) {
                cursor_ = utils::NextUtf8CodepointStart(text_, cursor_);
            } else {
                return false;
            }
        } else if (event == Event::Home || event == Event::End) {
            const auto& row = rows_[CursorRow()];
            cursor_ = event == Event::Home ? row.start : row.end;
        } else if (event == Event::ArrowUp || event == Event::ArrowDown ||
            event == Event::PageUp || event == Event::PageDown) {
            const size_t row = CursorRow();
            const size_t step = (event == Event::PageUp || event == Event::PageDown) ? Height() : 1;
            const size_t next = event == Event::ArrowUp || event == Event::PageUp
                ? row - std::min(row, step) : std::min(rows_.size() - 1, row + step);
            if (next == row && (event == Event::ArrowUp || event == Event::ArrowDown)) {
                return false;
            }
            const size_t column = utils::Utf8DisplayWidth(text_, rows_[row].start, cursor_);
            cursor_ = PositionInRow(next, column);
        } else {
            return false;
        }
        all_selected_ = false;
        follow_cursor_ = true;
        return true;
    }

private:
    size_t Height() const {
        return box_.y_max >= box_.y_min ? static_cast<size_t>(box_.y_max - box_.y_min + 1) : 20;
    }

    size_t MaxTop() const {
        return rows_.size() > Height() ? rows_.size() - Height() : 0;
    }

    void UpdateRows() {
        if (!rows_dirty_) {
            return;
        }
        rows_dirty_ = false;
        rows_.clear();
        size_t start = 0;
        while (true) {
            const size_t newline = text_.find('\n', start);
            const size_t limit = newline == std::string::npos ? text_.size() : newline;
            if (start == limit) {
                rows_.push_back({start, start});
            }
            while (start < limit) {
                size_t end = std::min(limit, utils::Utf8ByteIndexAtDisplayColumn(text_, start, columns_));
                if (end == start) {
                    end = std::min(limit, utils::NextUtf8CodepointStart(text_, start));
                }
                rows_.push_back({start, end});
                start = end;
            }
            if (newline == std::string::npos) {
                break;
            }
            start = newline + 1;
        }
        cursor_ = std::min(cursor_, text_.size());
    }

    size_t CursorRow() const {
        const auto next = std::upper_bound(rows_.begin(), rows_.end(), cursor_,
            [](size_t cursor, const auto& row) { return cursor < row.start; });
        return static_cast<size_t>(next - rows_.begin() - 1);
    }

    size_t PositionInRow(size_t index, size_t column) const {
        const auto& row = rows_[index];
        size_t position = std::min(row.end, utils::Utf8ByteIndexAtDisplayColumn(text_, row.start, column));
        if (position == row.end && position > row.start && index + 1 < rows_.size() &&
            rows_[index + 1].start == position) {
            position = utils::PreviousUtf8CodepointStart(text_, position);
        }
        return position;
    }

    ftxui::Element RenderRow(const utils::Utf8WrapSegment& row, bool cursor_row) const {
        using namespace ftxui;
        if (!cursor_row || !Focused() || all_selected_ || !editable_) {
            return text(text_.substr(row.start, row.end - row.start));
        }
        size_t position = cursor_;
        if (position == row.end && position > row.start &&
            utils::Utf8DisplayWidth(text_, row.start, position) >= static_cast<size_t>(columns_)) {
            position = utils::PreviousUtf8CodepointStart(text_, position);
        }
        const size_t next = position < row.end ? utils::NextUtf8CodepointStart(text_, position) : position;
        return hbox({
            text(text_.substr(row.start, position - row.start)),
            text(next > position ? text_.substr(position, next - position) : " ") |
                bgcolor(theme_.cursor) | color(theme_.modal_input_bg),
            text(text_.substr(next, row.end - next)),
        });
    }

    void SelectAll() {
        all_selected_ = !text_.empty();
        cursor_ = text_.size();
    }

    bool HandleMouse(ftxui::Event event) {
        using namespace ftxui;
        const auto& mouse = event.mouse();
        if (mouse.motion == Mouse::Released && dragging_scrollbar_) {
            dragging_scrollbar_ = false;
            return true;
        }
        if (!box_.Contain(mouse.x, mouse.y) && !dragging_scrollbar_) {
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
                last_click_ = {};
                all_selected_ = false;
            }
            return false;
        }
        if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
            top_ = mouse.button == Mouse::WheelUp
                ? top_ - std::min<size_t>(top_, 3) : std::min(MaxTop(), top_ + 3);
            follow_cursor_ = false;
            return true;
        }
        const bool pressed = mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed;
        if (dragging_scrollbar_ || (pressed && MaxTop() > 0 && mouse.x == box_.x_max)) {
            TakeFocus();
            dragging_scrollbar_ = true;
            last_click_ = {};
            const auto track = std::max<size_t>(1, Height() - 1);
            const auto offset = static_cast<size_t>(std::clamp(mouse.y, box_.y_min, box_.y_max) - box_.y_min);
            top_ = offset * MaxTop() / track;
            follow_cursor_ = false;
            return true;
        }
        if (!pressed) {
            return false;
        }
        TakeFocus();
        const auto now = std::chrono::steady_clock::now();
        if (now - last_click_ < std::chrono::milliseconds(450) &&
            mouse.x == last_click_x_ && mouse.y == last_click_y_) {
            SelectAll();
            last_click_ = {};
            return true;
        }
        last_click_ = now;
        last_click_x_ = mouse.x;
        last_click_y_ = mouse.y;
        all_selected_ = false;
        const size_t row = std::min(rows_.size() - 1, top_ + static_cast<size_t>(mouse.y - box_.y_min));
        cursor_ = PositionInRow(row, static_cast<size_t>(mouse.x - box_.x_min));
        return true;
    }

    std::string& text_;
    const Theme& theme_;
    int columns_;
    std::string placeholder_;
    bool editable_;
    ftxui::Box box_{1, 0, 1, 0};
    std::vector<utils::Utf8WrapSegment> rows_;
    size_t cursor_ = 0;
    size_t top_ = 0;
    bool rows_dirty_ = true;
    bool follow_cursor_ = true;
    bool all_selected_ = false;
    bool dragging_scrollbar_ = false;
    std::chrono::steady_clock::time_point last_click_{};
    int last_click_x_ = 0;
    int last_click_y_ = 0;
};

} // namespace textlt
