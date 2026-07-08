#include "bottom_bar_row.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

namespace {

const Theme& ResolveTheme(const Theme* theme) {
    static const Theme fallback_theme;
    return theme ? *theme : fallback_theme;
}

std::string TruncateLabel(const std::string& value, std::size_t max_length) {
    if (value.size() <= max_length) {
        return value;
    }
    if (max_length <= 3) {
        return value.substr(0, max_length);
    }
    return value.substr(0, max_length - 3) + "...";
}

int DisplayWidth(const std::string& value) {
    // Status labels are ASCII today, and branch/theme names are treated as a
    // compact hit-test approximation. Rendering remains correct for UTF-8 text.
    return static_cast<int>(value.size());
}

} // namespace

BottomBarRowComponent::BottomBarRowComponent(const Theme* theme, Callbacks callbacks)
    : theme_(theme), callbacks_(std::move(callbacks)) {}

BottomBarRowState BottomBarRowComponent::State() const {
    if (!callbacks_.state) {
        return BottomBarRowState{};
    }
    return callbacks_.state();
}

bool BottomBarRowComponent::HasBranchSegment() const {
    return !State().git_branch.empty();
}

bool BottomBarRowComponent::HasThemeSegment() const {
    return !State().theme_name.empty();
}

bool BottomBarRowComponent::Focusable() const {
    // The theme segment is always available, so the status row can be focused
    // without forcing a state refresh from Focusable().
    return true;
}

ftxui::Element BottomBarRowComponent::SegmentText(
    const std::string& value,
    Segment segment,
    bool clickable,
    const Theme& theme) const {
    using namespace ftxui;

    Element element = text(value) | color(theme.menu_foreground);
    if (!clickable) {
        return element;
    }

    const bool focused = Focused();
    const bool selected = focused &&
        focused_segment_index_ >= 0 &&
        focused_segment_index_ < static_cast<int>(segment_hit_boxes_.size()) &&
        segment_hit_boxes_[focused_segment_index_].segment == segment;

    element = element | underlined;
    if (selected) {
        element = element |
            bgcolor(theme.button_focused_bg) |
            color(theme.button_focused_fg);
    }
    return element;
}

ftxui::Element BottomBarRowComponent::Render() {
    using namespace ftxui;

    const Theme& theme = ResolveTheme(theme_);
    const BottomBarRowState state = State();
    const std::string branch = TruncateLabel(state.git_branch, 15);
    const bool has_branch = !branch.empty();
    const bool has_theme = !state.theme_name.empty();

    segment_hit_boxes_.clear();
    int x = 0;
    auto static_text = [&](const std::string& value) {
        x += DisplayWidth(value);
        return text(value) | color(theme.menu_foreground);
    };
    auto clickable_text = [&](const std::string& value, Segment segment, bool clickable) {
        if (clickable) {
            segment_hit_boxes_.push_back(SegmentHitBox{segment, x, x + DisplayWidth(value) - 1});
        }
        x += DisplayWidth(value);
        return SegmentText(value, segment, clickable, theme);
    };

    ftxui::Elements parts;
    parts.push_back(clickable_text(" Ln " + std::to_string(state.cursor_row) +
        ", Col " + std::to_string(state.cursor_col), Segment::Line, true));
    parts.push_back(static_text(" | " + state.line_ending));
    if (has_branch) {
        parts.push_back(static_text(" | branch: "));
        parts.push_back(clickable_text(branch, Segment::Branch, true));
    }
    parts.push_back(static_text(" | " + std::to_string(state.document_percent) + "%"));
    parts.push_back(static_text(" | Theme: "));
    parts.push_back(clickable_text(state.theme_name, Segment::Theme, has_theme));

    if (focused_segment_index_ >= static_cast<int>(segment_hit_boxes_.size())) {
        focused_segment_index_ = std::max(0, static_cast<int>(segment_hit_boxes_.size()) - 1);
    }

    return hbox(std::move(parts)) | reflect(box_);
}

void BottomBarRowComponent::ActivateSegment(Segment segment) {
    if (!callbacks_.run_command) {
        return;
    }
    if (segment == Segment::Line) {
        callbacks_.run_command("editor.go_to_line");
        return;
    }
    if (segment == Segment::Branch) {
        callbacks_.run_command("git.open");
        return;
    }
    callbacks_.run_command("theme.open");
}

bool BottomBarRowComponent::ActivateFocusedSegment() {
    if (segment_hit_boxes_.empty()) {
        return false;
    }
    if (focused_segment_index_ < 0 ||
        focused_segment_index_ >= static_cast<int>(segment_hit_boxes_.size())) {
        focused_segment_index_ = 0;
    }
    ActivateSegment(segment_hit_boxes_[focused_segment_index_].segment);
    return true;
}

bool BottomBarRowComponent::MoveFocus(int direction) {
    if (segment_hit_boxes_.empty()) {
        return false;
    }
    const int count = static_cast<int>(segment_hit_boxes_.size());
    focused_segment_index_ = (focused_segment_index_ + direction + count) % count;
    return true;
}

bool BottomBarRowComponent::SegmentAt(int x, int y, Segment& segment) {
    if (!box_.Contain(x, y)) {
        return false;
    }
    const int local_x = x - box_.x_min;
    for (int index = 0; index < static_cast<int>(segment_hit_boxes_.size()); ++index) {
        const SegmentHitBox& hit_box = segment_hit_boxes_[index];
        if (local_x >= hit_box.x_min && local_x <= hit_box.x_max) {
            focused_segment_index_ = index;
            segment = hit_box.segment;
            return true;
        }
    }
    return false;
}

bool BottomBarRowComponent::OnEvent(ftxui::Event event) {
    if (!Focusable()) {
        return false;
    }

    if (event.is_mouse()) {
        auto& mouse = event.mouse();
        if (!box_.Contain(mouse.x, mouse.y)) {
            return false;
        }
        TakeFocus();
        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            Segment segment;
            if (SegmentAt(mouse.x, mouse.y, segment)) {
                ActivateSegment(segment);
                return true;
            }
        }
        return mouse.button == ftxui::Mouse::Left;
    }

    if (event == ftxui::Event::ArrowLeft) {
        return MoveFocus(-1);
    }
    if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Tab) {
        return MoveFocus(1);
    }
    if (event == ftxui::Event::Return) {
        return ActivateFocusedSegment();
    }

    return false;
}

} // namespace textlt
