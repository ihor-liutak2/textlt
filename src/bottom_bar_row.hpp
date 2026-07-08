#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt {

struct BottomBarRowState {
    int cursor_row = 1;
    int cursor_col = 1;
    std::size_t total_lines = 1;
    int document_percent = 100;
    std::string line_ending;
    std::string git_branch;
    std::string theme_name;
};

class BottomBarRowComponent : public ftxui::ComponentBase {
public:
    struct Callbacks {
        std::function<BottomBarRowState()> state;
        std::function<void(const std::string& command_id)> run_command;
    };

    BottomBarRowComponent(const Theme* theme, Callbacks callbacks);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    void SetTheme(const Theme* theme) { theme_ = theme; }

private:
    enum class Segment {
        Line,
        Branch,
        Theme,
    };

    struct SegmentHitBox {
        Segment segment = Segment::Theme;
        int x_min = 0;
        int x_max = -1;
    };

    BottomBarRowState State() const;
    bool HasBranchSegment() const;
    bool HasThemeSegment() const;
    void ActivateSegment(Segment segment);
    bool ActivateFocusedSegment();
    bool MoveFocus(int direction);
    bool SegmentAt(int x, int y, Segment& segment);
    ftxui::Element SegmentText(
        const std::string& value,
        Segment segment,
        bool clickable,
        const Theme& theme) const;

    const Theme* theme_ = nullptr;
    Callbacks callbacks_;
    ftxui::Box box_;
    mutable std::vector<SegmentHitBox> segment_hit_boxes_;
    int focused_segment_index_ = 0;
};

} // namespace textlt
