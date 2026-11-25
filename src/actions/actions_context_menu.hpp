#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "actions/action_model.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt::actions {

class ActionsContextMenu {
public:
    struct Target {
        std::string group_id;
        std::string page_id;
        std::string item_id;
    };

    enum class Command { CopyBlock, Edit, Rename, MoveUp, MoveDown, CopyForConnect, Paste };
    enum class Result { Close, ClipboardFailure };
    using Execute = std::function<Result(const Target&, Command, size_t)>;

    explicit ActionsContextMenu(const Theme* theme);
    void Open(int x, int y, Target target, const ActionItem* item,
        bool can_move_up, bool can_move_down, bool can_paste);
    void Close();
    bool IsOpen() const { return target_.has_value(); }
    bool OnEvent(ftxui::Event event, const Execute& execute);
    ftxui::Element Overlay(ftxui::Element workspace);

private:
    struct Row {
        std::string label;
        std::optional<Command> command;
        size_t block = 0;
        bool enabled = true;
    };

    bool Enabled(int index) const;
    void Navigate(int delta);
    void Activate(int index, const Execute& execute);
    void Draw(ftxui::Screen& screen, const ftxui::Box& workspace);

    const Theme* theme_;
    std::optional<Target> target_;
    std::vector<Row> rows_;
    int anchor_x_ = 0;
    int anchor_y_ = 0;
    int selected_ = -1;
    int pressed_ = -1;
    int first_row_ = 0;
    int visible_rows_ = 0;
    bool clipboard_failed_ = false;
    ftxui::Box popup_{0, -1, 0, -1};
};

} // namespace textlt::actions
