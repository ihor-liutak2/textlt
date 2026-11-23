#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt::notes {

class NotesContextMenu {
public:
    enum class Command {
        Undo, Redo, Cut, Copy, Paste, SelectAll,
        NormalText, Heading1, Heading2, Heading3, BulletedList, NumberedList,
    };

    struct Item {
        std::string label;
        std::optional<Command> command;
        bool enabled = false;
    };

    explicit NotesContextMenu(const Theme* theme);
    void Open(int x, int y, std::vector<Item> items);
    void Close();
    bool IsOpen() const { return !items_.empty(); }
    bool OnEvent(ftxui::Event event, const std::function<void(Command)>& execute);
    ftxui::Element Overlay(ftxui::Element workspace);

private:
    void Navigate(int step);
    bool Enabled(int index) const;
    ftxui::Element Layout(const ftxui::Box& workspace);

    const Theme* theme_;
    std::vector<Item> items_;
    int x_ = 0;
    int y_ = 0;
    int width_ = 0;
    int active_ = -1;
    int pressed_ = -1;
    int scroll_ = 0;
    ftxui::Box box_{0, -1, 0, -1};
};

} // namespace textlt::notes
