#include "editor_input_controller.hpp"

#include <cstddef>
#include <string>

#include "editor_component.hpp"
#include "keyboard_shortcuts.hpp"

namespace textlt {
namespace {

enum class NavigationAction {
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    ParagraphUp,
    ParagraphDown,
    WordLeft,
    WordRight,
};

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
    if (input == "Shift+Ctrl+Left" ||
        input == "Ctrl+Shift+Left" ||
        input == "\x1B[1;6D" ||
        input == "\x1B[1;10D" ||
        input == "\x1B[27;6;68~" ||
        input == "\x1B[68;6u") {
        *action = NavigationAction::WordLeft;
        return true;
    }
    if (input == "Shift+Ctrl+Right" ||
        input == "Ctrl+Shift+Right" ||
        input == "\x1B[1;6C" ||
        input == "\x1B[1;10C" ||
        input == "\x1B[27;6;67~" ||
        input == "\x1B[67;6u") {
        *action = NavigationAction::WordRight;
        return true;
    }
    if (input == "Shift+Ctrl+Up" ||
        input == "Ctrl+Shift+Up" ||
        input == "\x1B[1;6A" ||
        input == "\x1B[1;10A" ||
        input == "\x1B[27;6;65~" ||
        input == "\x1B[65;6u") {
        *action = NavigationAction::ParagraphUp;
        return true;
    }
    if (input == "Shift+Ctrl+Down" ||
        input == "Ctrl+Shift+Down" ||
        input == "\x1B[1;6B" ||
        input == "\x1B[1;10B" ||
        input == "\x1B[27;6;66~" ||
        input == "\x1B[66;6u") {
        *action = NavigationAction::ParagraphDown;
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
    if (input == "\x1B[1;2~" || input == "\x1B[2H") {
        *action = NavigationAction::Home;
        return true;
    }
    if (input == "\x1B[1;2F" || input == "\x1B[8;2~") {
        *action = NavigationAction::End;
        return true;
    }
    if (input == "\x1B[4;2~" || input == "\x1B[2F") {
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
    if (event == ftxui::Event::PageUp) {
        *action = NavigationAction::PageUp;
        return true;
    }
    if (event == ftxui::Event::PageDown) {
        *action = NavigationAction::PageDown;
        return true;
    }
    if (event.input() == "\x1B[5~") {
        *action = NavigationAction::PageUp;
        return true;
    }
    if (event.input() == "\x1B[6~") {
        *action = NavigationAction::PageDown;
        return true;
    }
    if (event == ftxui::Event::ArrowUpCtrl ||
        event.input() == "Ctrl+Up" ||
        event.input() == "\x1B[1;5A" ||
        event.input() == "\x1B[27;5;65~" ||
        event.input() == "\x1B[65;5u") {
        *action = NavigationAction::ParagraphUp;
        return true;
    }
    if (event == ftxui::Event::ArrowDownCtrl ||
        event.input() == "Ctrl+Down" ||
        event.input() == "\x1B[1;5B" ||
        event.input() == "\x1B[27;5;66~" ||
        event.input() == "\x1B[66;5u") {
        *action = NavigationAction::ParagraphDown;
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

bool IsWordDeleteBackwardEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Alt+Backspace" ||
        input == "Ctrl+Backspace" ||
        input == "\x1B[127;5u" ||
        input == "\x1B[8;5u" ||
        input == "\x1B[127;5~" ||
        input == "\x1B[8;5~" ||
        input == "\x1B[27;5;127~" ||
        input == "\x1B[27;5;8~" ||
        input == "\x1B\x7F" ||
        input == "\x1B\x08" ||
        input == "\x17" ||
        event == ftxui::Event::Special("Alt+Backspace") ||
        event == ftxui::Event::Special("Ctrl+Backspace") ||
        event == ftxui::Event::Special("\x1B[127;5u") ||
        event == ftxui::Event::Special("\x1B[8;5u") ||
        event == ftxui::Event::Special("\x1B[127;5~") ||
        event == ftxui::Event::Special("\x1B[8;5~") ||
        event == ftxui::Event::Special("\x1B[27;5;127~") ||
        event == ftxui::Event::Special("\x1B[27;5;8~") ||
        event == ftxui::Event::Special("\x1B\x7F") ||
        event == ftxui::Event::Special("\x1B\x08") ||
        event == ftxui::Event::Special("\x17");
}

bool IsCtrlDeleteEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Ctrl+Delete" ||
        input == "\x1B[3;5~" ||
        input == "\x1B[3;5u" ||
        input == "\x1B[27;5;3~" ||
        event == ftxui::Event::Special("Ctrl+Delete") ||
        event == ftxui::Event::Special("\x1B[3;5~") ||
        event == ftxui::Event::Special("\x1B[3;5u") ||
        event == ftxui::Event::Special("\x1B[27;5;3~");
}

bool IsShiftTabEvent(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Shift+Tab" ||
        input == "\x1B[Z" ||
        input == "\x1B[1;2Z" ||
        event == ftxui::Event::Special("Shift+Tab") ||
        event == ftxui::Event::Special("\x1B[Z") ||
        event == ftxui::Event::Special("\x1B[1;2Z");
}

bool HasMultipleUtf8Codepoints(const std::string& input) {
    size_t count = 0;
    for (unsigned char character : input) {
        if ((character & 0xC0) != 0x80) {
            ++count;
            if (count > 1) {
                return true;
            }
        }
    }
    return false;
}

bool IsPrintableRawTextInput(const ftxui::Event& event) {
    if (event.is_character() || event.is_mouse() || event.is_cursor_reporting()) {
        return false;
    }

    const std::string& input = event.input();
    if (input.empty()) {
        return false;
    }
    if (input.find('\x1B') != std::string::npos ||
        input.find('\n') != std::string::npos ||
        input.find('\r') != std::string::npos ||
        input.find('\t') != std::string::npos) {
        return false;
    }

    bool has_non_ascii = false;
    for (unsigned char character : input) {
        if (character < 0x20 || character == 0x7F) {
            return false;
        }
        if (character >= 0x80) {
            has_non_ascii = true;
        }
    }
    return has_non_ascii;
}

} // namespace

bool EditorInputController::HandleEvent(EditorComponent& editor, ftxui::Event event) {
    const std::string& input = event.input();
    const bool read_only = editor.IsReadOnly();
    if (event == ftxui::Event::Tab) {
        if (read_only) {
            return true;
        }
        if (editor.HasSelection()) {
            editor.IndentLines();
            return true;
        }

        const int configured_tab_size = editor.config_ ? editor.config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;
        editor.InsertText(std::string(tab_size, ' '));
        return true;
    }

    if (event.is_character() && editor.session_) {
        if (read_only) {
            return true;
        }
        const std::string& event_input = event.input();
        if (HasMultipleUtf8Codepoints(event_input) ||
            event_input.find('\n') != std::string::npos ||
            event_input.find('\r') != std::string::npos) {
            editor.InsertText(event_input);
            return true;
        }

        if ((!editor.config_ || editor.config_->auto_pairing) &&
            editor.HandleAutoPairCharacter(event.input())) {
            return true;
        }

        editor.SaveSnapshotForTyping(event_input);
        if (editor.HasSelection()) {
            editor.DeleteSelectionWithoutSnapshot();
        }
        editor.session_->InsertCharacter(event_input);
        editor.ClearSelection();
        editor.UpdateScroll();
        return true;
    }

    if (IsPrintableRawTextInput(event) && editor.session_) {
        if (!read_only) {
            editor.InsertText(event.input());
        }
        return true;
    }

    if (event == ftxui::Event::Backspace && editor.session_) {
        if (read_only) {
            return true;
        }
        editor.EndTypingGroup();
        if (editor.HasSelection()) {
            editor.DeleteSelection();
            return true;
        }

        if (editor.session_->Backspace()) {
            editor.ClearSelection();
            editor.UpdateScroll();
        }
        return true;
    }

    if (event == ftxui::Event::Delete && editor.session_) {
        if (read_only) {
            return true;
        }
        editor.EndTypingGroup();
        if (editor.HasSelection()) {
            editor.DeleteSelection();
            return true;
        }

        if (editor.session_->DeleteForward()) {
            editor.ClearSelection();
            editor.UpdateScroll();
        }
        return true;
    }

    if (event == ftxui::Event::Return) {
        return read_only || editor.HandleAutoIndentReturn();
    }

    NavigationAction action = NavigationAction::Left;
    const bool extend_selection = false;
    if (IsNavigationEvent(event, &action)) {
        editor.EndTypingGroup();
        if (extend_selection) {
            editor.BeginSelection();
        }

        switch (action) {
            case NavigationAction::Left: editor.MoveCursorLeft(); break;
            case NavigationAction::Right: editor.MoveCursorRight(); break;
            case NavigationAction::Up: editor.MoveCursorUp(); break;
            case NavigationAction::Down: editor.MoveCursorDown(); break;
            case NavigationAction::Home: editor.MoveCursorHome(); break;
            case NavigationAction::End: editor.MoveCursorEnd(); break;
            case NavigationAction::PageUp: editor.MoveCursorPageUp(); break;
            case NavigationAction::PageDown: editor.MoveCursorPageDown(); break;
            case NavigationAction::ParagraphUp: editor.MoveCursorToPreviousParagraph(); break;
            case NavigationAction::ParagraphDown: editor.MoveCursorToNextParagraph(); break;
            case NavigationAction::WordLeft: editor.MoveCursorToPreviousWord(); break;
            case NavigationAction::WordRight: editor.MoveCursorToNextWord(); break;
        }

        editor.ClampCursorToBuffer();
        if (!extend_selection) {
            editor.ClearSelection();
        }
        editor.UpdateScroll();
        return true;
    }

    return false;
}

} // namespace textlt
