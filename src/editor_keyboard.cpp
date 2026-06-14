#include "editor_component.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/event.hpp"

namespace textlt {
namespace {

enum class NavigationAction {
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    WordLeft,
    WordRight,
};

struct Position {
    size_t x = 0;
    size_t y = 0;
};

bool PositionLess(const Position& left, const Position& right) {
    return left.y < right.y || (left.y == right.y && left.x < right.x);
}

Position ClampedPosition(Position position, const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return {};
    }

    position.y = std::min(position.y, lines.size() - 1);
    position.x = std::min(position.x, lines[position.y].size());
    return position;
}

std::pair<Position, Position> OrderedSelection(Position anchor,
                                               Position cursor,
                                               const std::vector<std::string>& lines) {
    anchor = ClampedPosition(anchor, lines);
    cursor = ClampedPosition(cursor, lines);
    if (PositionLess(cursor, anchor)) {
        std::swap(anchor, cursor);
    }
    return {anchor, cursor};
}

bool IsShiftNavigationEvent(const ftxui::Event& event, NavigationAction* action) {
    const std::string& input = event.input();

    if (input == "\x1B[1;2D") {
        *action = NavigationAction::Left;
        return true;
    }
    if (input == "\x1B[1;2C") {
        *action = NavigationAction::Right;
        return true;
    }
    if (input == "\x1B[1;6D") {
        *action = NavigationAction::WordLeft;
        return true;
    }
    if (input == "\x1B[1;6C") {
        *action = NavigationAction::WordRight;
        return true;
    }
    if (input == "\x1B[1;2A") {
        *action = NavigationAction::Up;
        return true;
    }
    if (input == "\x1B[1;2B") {
        *action = NavigationAction::Down;
        return true;
    }
    if (input == "\x1B[1;2H" || input == "\x1B[7;2~") {
        *action = NavigationAction::Home;
        return true;
    }
    if (input == "\x1B[1;2F" || input == "\x1B[8;2~") {
        *action = NavigationAction::End;
        return true;
    }

    return false;
}

bool IsNavigationEvent(const ftxui::Event& event, NavigationAction* action) {
    if (event == ftxui::Event::ArrowLeft) {
        *action = NavigationAction::Left;
        return true;
    }
    if (event == ftxui::Event::ArrowRight) {
        *action = NavigationAction::Right;
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        *action = NavigationAction::Up;
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        *action = NavigationAction::Down;
        return true;
    }
    if (event == ftxui::Event::Home) {
        *action = NavigationAction::Home;
        return true;
    }
    if (event == ftxui::Event::End) {
        *action = NavigationAction::End;
        return true;
    }
    if (event == ftxui::Event::ArrowLeftCtrl) {
        *action = NavigationAction::WordLeft;
        return true;
    }
    if (event == ftxui::Event::ArrowRightCtrl) {
        *action = NavigationAction::WordRight;
        return true;
    }

    return false;
}

bool IsMoveLinesUpEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Alt+Up" ||
        input == "\x1B[1;3A" ||
        input == "\x1B[1;9A" ||
        input == "\x1B[27;3;65~" ||
        input == "\x1B[65;3u";
}

bool IsMoveLinesDownEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Alt+Down" ||
        input == "\x1B[1;3B" ||
        input == "\x1B[1;9B" ||
        input == "\x1B[27;3;66~" ||
        input == "\x1B[66;3u";
}

bool IsDuplicateLinesEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Shift+Alt+Down" ||
        input == "Alt+Shift+Down" ||
        input == "\x1B[1;4B" ||
        input == "\x1B[1;10B" ||
        input == "\x1B[27;4;66~" ||
        input == "\x1B[66;4u";
}

bool IsCtrlBackspaceEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Ctrl+Backspace" ||
        input == "\x08" ||
        input == "\x1B[127;5u" ||
        input == "\x1B[8;5u" ||
        input == "\x1B[27;5;127~" ||
        input == "\x1B[27;5;8~" ||
        input == "\x17" ||
        event == ftxui::Event::Special("\x08") ||
        event == ftxui::Event::Special("\x1B[127;5u") ||
        event == ftxui::Event::Special("\x1B[8;5u");
}

bool IsCtrlDeleteEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Ctrl+Delete" ||
        input == "\x1B[3;5~" ||
        input == "\x1B[3;5u" ||
        event == ftxui::Event::Special("\x1B[3;5~") ||
        event == ftxui::Event::Special("\x1B[3;5u");
}


} // namespace

bool EditorComponent::OnEvent(ftxui::Event event) {
    if (event.is_mouse() && HandleMouseEvent(event)) {
        return true;
    }

    const std::string& input = event.input();
    if (input == "\x1A" || input == "Ctrl+Z") {
        Undo();
        return true;
    }
    if (input == "\x19" || input == "Ctrl+Y") {
        Redo();
        return true;
    }
    if (input == "\x1F" || input == "Ctrl+/" || event == ftxui::Event::Special("\x1F")) {
        ToggleComment();
        return true;
    }
    if (input == "\x14" || input == "Ctrl+T") {
        ToggleCase();
        return true;
    }

    if (IsDuplicateLinesEvent(event)) {
        return DuplicateLines();
    }
    if (IsMoveLinesUpEvent(event)) {
        return MoveLinesUp();
    }
    if (IsMoveLinesDownEvent(event)) {
        return MoveLinesDown();
    }

    if (event == ftxui::Event::Tab) {
        const int configured_tab_size = config_ ? config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;
        InsertText(std::string(tab_size, ' '));
        return true;
    }

    if (IsCtrlBackspaceEvent(event)) {
        DeleteWordBackward();
        return true;
    }

    if (IsCtrlDeleteEvent(event)) {
        DeleteWordForward();
        return true;
    }

    if (event.is_character()) {
        if ((!config_ || config_->auto_pairing) && HandleAutoPairCharacter(event.input())) {
            return true;
        }

        // Insert incoming characters at the current cursor position.
        SaveSnapshotForTyping(event.input());
        if (HasSelection()) {
            DeleteSelectionWithoutSnapshot();
        }
        text_lines_[cursor_y_].insert(cursor_x_, event.input());
        cursor_x_ += event.input().size();
        is_dirty_ = true;
        ClearSelection();
        UpdateScroll();
        return true;
    }

    if (event == ftxui::Event::Backspace) {
        EndTypingGroup();
        if (HasSelection()) {
            DeleteSelection();
            return true;
        }

        // Delete the character before the cursor or join with the previous line.
        if (cursor_x_ > 0) {
            SaveSnapshot();
            text_lines_[cursor_y_].erase(cursor_x_ - 1, 1);
            cursor_x_--;
        } else if (cursor_y_ > 0) {
            SaveSnapshot();
            cursor_x_ = text_lines_[cursor_y_ - 1].size();
            text_lines_[cursor_y_ - 1] += text_lines_[cursor_y_];
            text_lines_.erase(text_lines_.begin() + cursor_y_);
            cursor_y_--;
        }
        ClearSelection();
        UpdateScroll();
        return true;
    }

    if (event == ftxui::Event::Delete) {
        EndTypingGroup();
        if (HasSelection()) {
            DeleteSelection();
            return true;
        }

        if (cursor_x_ < text_lines_[cursor_y_].size()) {
            SaveSnapshot();
            text_lines_[cursor_y_].erase(cursor_x_, 1);
        } else if (cursor_y_ + 1 < text_lines_.size()) {
            SaveSnapshot();
            text_lines_[cursor_y_] += text_lines_[cursor_y_ + 1];
            text_lines_.erase(text_lines_.begin() + static_cast<std::ptrdiff_t>(cursor_y_ + 1));
        }
        ClearSelection();
        UpdateScroll();
        return true;
    }

    if (event == ftxui::Event::Return) {
        return HandleAutoIndentReturn();
    }

    NavigationAction action = NavigationAction::Left;
    const bool extend_selection = IsShiftNavigationEvent(event, &action);
    if (extend_selection || IsNavigationEvent(event, &action)) {
        EndTypingGroup();
        if (extend_selection) {
            BeginSelection();
        }

        switch (action) {
            case NavigationAction::Left:
                MoveCursorLeft();
                break;
            case NavigationAction::Right:
                MoveCursorRight();
                break;
            case NavigationAction::Up:
                MoveCursorUp();
                break;
            case NavigationAction::Down:
                MoveCursorDown();
                break;
            case NavigationAction::Home:
                MoveCursorHome();
                break;
            case NavigationAction::End:
                MoveCursorEnd();
                break;
            case NavigationAction::WordLeft:
                MoveCursorToPreviousWord();
                break;
            case NavigationAction::WordRight:
                MoveCursorToNextWord();
                break;
        }

        ClampCursorToBuffer();
        if (!extend_selection) {
            ClearSelection();
        }
        UpdateScroll();
        return true;
    }

    return ComponentBase::OnEvent(event);
}


} // namespace textlt
