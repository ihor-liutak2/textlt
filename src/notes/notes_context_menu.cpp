#include "notes/notes_context_menu.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/string.hpp"

namespace textlt::notes {
namespace {

// The Notes body sets the layout bounds. The overlay never changes its size.
class NotesMenuOverlay final : public ftxui::Node {
public:
    using DrawMenu = std::function<void(ftxui::Screen&, ftxui::Box)>;

    NotesMenuOverlay(ftxui::Element body, DrawMenu draw_menu)
        : Node({std::move(body)}), draw_menu_(std::move(draw_menu)) {}

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        children_.front()->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        Node::Render(screen);
        draw_menu_(screen, box_);
    }

private:
    DrawMenu draw_menu_;
};

int PlaceNearClick(int anchor, int extent, int low, int high) {
    const int last = high - extent + 1;
    anchor = std::clamp(anchor, low, high);
    return anchor <= last ? anchor : std::clamp(anchor - extent + 1, low, last);
}

} // namespace

NotesContextMenu::NotesContextMenu(const Theme* theme) : theme_(theme) {}

void NotesContextMenu::Open(int x, int y, std::vector<Item> items) {
    Close();
    x_ = x;
    y_ = y;
    items_ = std::move(items);
    width_ = 0;
    for (const auto& item : items_) {
        width_ = std::max(width_, ftxui::string_width(item.label) + 4);
    }
    Navigate(1);
}

void NotesContextMenu::Close() {
    items_.clear();
    active_ = -1;
    pressed_ = -1;
    scroll_ = 0;
    box_ = {0, -1, 0, -1};
}

bool NotesContextMenu::Enabled(int index) const {
    return index >= 0 && index < static_cast<int>(items_.size()) &&
        items_[index].command.has_value() && items_[index].enabled;
}

void NotesContextMenu::Navigate(int step) {
    const int count = static_cast<int>(items_.size());
    int next = active_ < 0 && step < 0 ? 0 : active_;
    for (int attempt = 0; attempt < count; ++attempt) {
        next = (next + step + count) % count;
        if (Enabled(next)) {
            active_ = next;
            return;
        }
    }
}

ftxui::Element NotesContextMenu::Overlay(ftxui::Element workspace) {
    return std::make_shared<NotesMenuOverlay>(std::move(workspace),
        [this](ftxui::Screen& screen, ftxui::Box box) {
            const ftxui::Box terminal{0, screen.dimx() - 1, 0, screen.dimy() - 1};
            if (auto menu = Layout(ftxui::Box::Intersection(box, terminal))) {
                menu->Render(screen);
                // Popup borders must not join the underlying editor or sidebar borders.
                for (int y = box_.y_min; y <= box_.y_max; ++y) {
                    for (int x = box_.x_min; x <= box_.x_max; ++x) {
                        screen.CellAt(x, y).automerge = false;
                    }
                }
            }
        });
}

ftxui::Element NotesContextMenu::Layout(const ftxui::Box& workspace) {
    using namespace ftxui;
    const int width = std::min(width_, workspace.x_max - workspace.x_min + 1);
    const int count = static_cast<int>(items_.size());
    const int visible = std::min(count, workspace.y_max - workspace.y_min - 1);
    if (width < 3 || visible < 1) {
        box_ = {0, -1, 0, -1};
        return {};
    }

    if (active_ >= 0) {
        scroll_ = std::clamp(scroll_, std::max(0, active_ - visible + 1), active_);
    }
    scroll_ = std::min(scroll_, count - visible);
    const int height = visible + 2;
    const int x = PlaceNearClick(x_, width, workspace.x_min, workspace.x_max);
    const int y = PlaceNearClick(y_, height, workspace.y_min, workspace.y_max);
    box_ = {x, x + width - 1, y, y + height - 1};

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    for (int index = scroll_; index < scroll_ + visible; ++index) {
        const auto& item = items_[index];
        if (!item.command) {
            rows.push_back(separator() | color(theme.modal_border));
            continue;
        }
        Element row = hbox({text(" " + item.label), filler()});
        if (index == active_) {
            row = row | bgcolor(theme.modal_selected_item_bg) | color(theme.modal_selected_item_fg);
        } else if (item.enabled) {
            row = row | color(theme.modal_text_color);
        } else {
            row = row | color(theme.button_disabled_fg) | dim;
        }
        rows.push_back(std::move(row));
    }
    auto popup = vbox(std::move(rows)) | borderStyled(LIGHT, theme.modal_border) |
        bgcolor(theme.modal_background) | clear_under;
    popup->ComputeRequirement();
    popup->SetBox(box_);
    return popup;
}

bool NotesContextMenu::OnEvent(ftxui::Event event,
                               const std::function<void(Command)>& execute) {
    using namespace ftxui;
    // FTXUI reports a terminal resize as a special NUL event.
    if (!IsOpen() || event == Event::Custom || event.is_cursor_position() ||
        (event.input().size() == 1 && event.input().front() == '\0')) return false;

    int chosen = -1;
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        const bool wheel = mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown;
        if (!box_.Contain(mouse.x, mouse.y)) {
            if (mouse.motion == Mouse::Released) pressed_ = -1;
            if (wheel || mouse.motion == Mouse::Pressed) Close();
            return !wheel;
        }
        const int row = mouse.y > box_.y_min && mouse.y < box_.y_max &&
            mouse.x > box_.x_min && mouse.x < box_.x_max
            ? scroll_ + mouse.y - box_.y_min - 1 : -1;
        if (wheel) {
            Navigate(mouse.button == Mouse::WheelUp ? -1 : 1);
        } else if (mouse.motion == Mouse::Moved && Enabled(row)) {
            active_ = row;
        } else if (mouse.button == Mouse::Left) {
            if (mouse.motion == Mouse::Pressed) {
                pressed_ = Enabled(row) ? row : -1;
                if (pressed_ >= 0) active_ = pressed_;
            } else if (mouse.motion == Mouse::Released && std::exchange(pressed_, -1) == row) {
                chosen = row;
            }
        }
    } else if (event == Event::ArrowUp || event == Event::ArrowDown) {
        Navigate(event == Event::ArrowUp ? -1 : 1);
    } else if (event == Event::Return) {
        if (!box_.IsEmpty()) chosen = active_;
    } else {
        Close();
        return event == Event::Escape;
    }

    if (Enabled(chosen)) {
        const Command command = *items_[chosen].command;
        Close();
        execute(command);
    }
    return true;
}

} // namespace textlt::notes
