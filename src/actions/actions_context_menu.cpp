#include "actions/actions_context_menu.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt::actions {
namespace {

constexpr char kClipboardError[] = "Clipboard write failed";

class ActionsMenuLayer final : public ftxui::Node {
public:
    using Paint = std::function<void(ftxui::Screen&, const ftxui::Box&)>;

    ActionsMenuLayer(ftxui::Element workspace, Paint paint)
        : Node({std::move(workspace)}), paint_(std::move(paint)) {}

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        children_.front()->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        Node::Render(screen);
        paint_(screen, box_);
    }

private:
    Paint paint_;
};

int FitNear(int anchor, int extent, int low, int high) {
    const int last_start = high - extent + 1;
    return std::clamp(anchor > last_start ? anchor - extent + 1 : anchor, low, last_start);
}

} // namespace

ActionsContextMenu::ActionsContextMenu(const Theme* theme) : theme_(theme) {}

void ActionsContextMenu::Open(int x, int y, Target target, const ActionItem* item,
    bool can_move_up, bool can_move_down, bool can_paste) {
    Close();
    anchor_x_ = x;
    anchor_y_ = y;
    target_ = std::move(target);
    if (item) {
        for (size_t block = 0; block < item->tabs.size(); ++block) {
            rows_.push_back({item->tabs[block].caption, Command::CopyBlock, block});
        }
        rows_.push_back({});
        rows_.push_back({"Edit Action", Command::Edit});
        rows_.push_back({"Rename Action", Command::Rename});
        rows_.push_back({"Move Up", Command::MoveUp, 0, can_move_up});
        rows_.push_back({"Move Down", Command::MoveDown, 0, can_move_down});
        rows_.push_back({"Copy for Connect", Command::CopyForConnect});
    }
    rows_.push_back({"Paste", Command::Paste, 0, can_paste});
    Navigate(1);
}

void ActionsContextMenu::Close() {
    target_.reset();
    rows_.clear();
    selected_ = pressed_ = -1;
    first_row_ = 0;
    visible_rows_ = 0;
    clipboard_failed_ = false;
    popup_ = {0, -1, 0, -1};
}

bool ActionsContextMenu::Enabled(int index) const {
    return index >= 0 && index < static_cast<int>(rows_.size()) &&
        rows_[index].enabled && rows_[index].command.has_value();
}

void ActionsContextMenu::Navigate(int delta) {
    const int count = static_cast<int>(rows_.size());
    int index = selected_ < 0 ? (delta > 0 ? count - 1 : 0) : selected_;
    for (int step = 0; step < count; ++step) {
        index = (index + delta + count) % count;
        if (Enabled(index)) {
            selected_ = index;
            break;
        }
    }
}

void ActionsContextMenu::Activate(int index, const Execute& execute) {
    if (!Enabled(index) || popup_.IsEmpty()) return;
    const Target target = *target_;
    if (execute(target, *rows_[index].command, rows_[index].block) == Result::Close) Close();
    else clipboard_failed_ = true;
}

bool ActionsContextMenu::OnEvent(ftxui::Event event, const Execute& execute) {
    using namespace ftxui;
    if (!IsOpen() || event == Event::Custom || event.is_cursor_position() ||
        (event.input().size() == 1 && event.input().front() == '\0')) return false;

    if (!event.is_mouse()) {
        if (event == Event::ArrowUp || event == Event::ArrowDown) {
            Navigate(event == Event::ArrowUp ? -1 : 1);
        } else if (event == Event::Return) {
            Activate(selected_, execute);
        } else {
            Close();
            return event == Event::Escape;
        }
        return true;
    }

    const auto& mouse = event.mouse();
    if (!popup_.Contain(mouse.x, mouse.y)) {
        if (mouse.motion == Mouse::Pressed) Close();
        pressed_ = -1;
        return true;
    }
    const int index = mouse.x > popup_.x_min && mouse.x < popup_.x_max &&
        mouse.y > popup_.y_min && mouse.y <= popup_.y_min + visible_rows_
        ? first_row_ + mouse.y - popup_.y_min - 1 : -1;
    if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
        Navigate(mouse.button == Mouse::WheelUp ? -1 : 1);
    } else if (mouse.motion == Mouse::Moved && Enabled(index)) {
        selected_ = index;
    } else if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        pressed_ = Enabled(index) ? index : -1;
        if (pressed_ >= 0) selected_ = pressed_;
    } else if (mouse.button == Mouse::Left && mouse.motion == Mouse::Released &&
               std::exchange(pressed_, -1) == index) {
        Activate(index, execute);
    }
    return true;
}

ftxui::Element ActionsContextMenu::Overlay(ftxui::Element workspace) {
    return std::make_shared<ActionsMenuLayer>(std::move(workspace),
        [this](ftxui::Screen& screen, const ftxui::Box& box) { Draw(screen, box); });
}

void ActionsContextMenu::Draw(ftxui::Screen& screen, const ftxui::Box& workspace) {
    using namespace ftxui;
    if (!IsOpen()) return;
    const int top = std::max(0, workspace.y_min);
    const int bottom = std::min(screen.dimy() - 1, workspace.y_max);
    const int available_rows = bottom - top - 1;
    if (screen.dimx() < 3 || available_rows < 1) {
        popup_ = {0, -1, 0, -1};
        return;
    }
    const bool show_error = clipboard_failed_ && available_rows > 1;
    const int count = static_cast<int>(rows_.size());
    const int visible = std::min(count, available_rows - static_cast<int>(show_error));
    visible_rows_ = visible;
    first_row_ = std::clamp(first_row_, std::max(0, selected_ - visible + 1), selected_);
    first_row_ = std::min(first_row_, count - visible);
    int label_width = show_error ? string_width(kClipboardError) : 0;
    for (const auto& row : rows_) label_width = std::max(label_width, string_width(row.label));
    const int width = std::min(screen.dimx(), label_width + 4);
    const int height = visible + 2 + static_cast<int>(show_error);
    const int x = FitNear(anchor_x_, width, 0, screen.dimx() - 1);
    const int y = FitNear(anchor_y_, height, top, bottom);
    popup_ = {x, x + width - 1, y, y + height - 1};

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements elements;
    for (int index = first_row_; index < first_row_ + visible; ++index) {
        const auto& row = rows_[index];
        if (!row.command) {
            elements.push_back(separator() | color(theme.modal_border));
            continue;
        }
        auto label = hbox({text(" " + row.label), filler()});
        if (index == selected_) {
            label = label | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg);
        } else {
            label = row.enabled ? label | color(theme.modal_text_color)
                                : label | color(theme.button_disabled_fg) | dim;
        }
        elements.push_back(std::move(label));
    }
    if (show_error) elements.push_back(text(std::string(" ") + kClipboardError) | color(theme.button_warning));
    auto menu = vbox(std::move(elements)) | borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background) | clear_under;
    menu->ComputeRequirement();
    menu->SetBox(popup_);
    menu->Render(screen);
    // Keep the popup border separate from the workspace borders underneath it.
    for (int row = popup_.y_min; row <= popup_.y_max; ++row) {
        for (int col = popup_.x_min; col <= popup_.x_max; ++col) {
            screen.CellAt(col, row).automerge = false;
        }
    }
}

} // namespace textlt::actions
