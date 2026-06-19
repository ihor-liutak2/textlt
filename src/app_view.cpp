#include "app.hpp"

#include <algorithm>
#include <filesystem>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include <ftxui/component/component_base.hpp>
#include "file_utils.hpp"
#include "theme.hpp"

using namespace ftxui;

namespace textlt {

namespace {

bool IsLineManipulationShortcut(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Alt+Up" ||
        input == "Alt+Down" ||
        input == "Shift+Alt+Down" ||
        input == "Alt+Shift+Down" ||
        input == "\x1B[1;3A" ||
        input == "\x1B[1;9A" ||
        input == "\x1B[27;3;65~" ||
        input == "\x1B[65;3u" ||
        input == "\x1B[1;3B" ||
        input == "\x1B[1;9B" ||
        input == "\x1B[27;3;66~" ||
        input == "\x1B[66;3u" ||
        input == "\x1B[1;4B" ||
        input == "\x1B[1;10B" ||
        input == "\x1B[27;4;66~" ||
        input == "\x1B[66;4u";
}

bool IsWordDeleteBackwardShortcut(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "\x17" ||
        input == "Alt+Backspace" ||
        input == "Ctrl+Backspace" ||
        input == "\x1B[127;5u" ||
        input == "\x1B[8;5u" ||
        input == "\x1B[127;5~" ||
        input == "\x1B[8;5~" ||
        input == "\x1B[27;5;127~" ||
        input == "\x1B[27;5;8~" ||
        input == "\x1B\x7F" ||
        input == "\x1B\x08" ||
        event == ftxui::Event::Special("Alt+Backspace") ||
        event == ftxui::Event::Special("Ctrl+Backspace") ||
        event == ftxui::Event::Special("\x17") ||
        event == ftxui::Event::Special("\x1B[127;5u") ||
        event == ftxui::Event::Special("\x1B[8;5u") ||
        event == ftxui::Event::Special("\x1B[127;5~") ||
        event == ftxui::Event::Special("\x1B[8;5~") ||
        event == ftxui::Event::Special("\x1B[27;5;127~") ||
        event == ftxui::Event::Special("\x1B[27;5;8~") ||
        event == ftxui::Event::Special("\x1B\x7F") ||
        event == ftxui::Event::Special("\x1B\x08");
}

bool IsWordDeleteForwardShortcut(const ftxui::Event& event) {
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

} // namespace

ftxui::Element TextltApp::Render() {
    using namespace ftxui;

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    UpdateFileMenuLabels();
    RefreshOpenedDocumentsSidebar();
    size_t bottom_overlay_rows = 2; // Status separator + status bar.
    if (current_search_mode_ != SearchMode::None) {
        bottom_overlay_rows += 7; // Find separator plus the bordered multi-row search panel.
    }
    if (show_goto_line_bar_) {
        bottom_overlay_rows += 4; // Go-to-line separator + bordered panel.
    }
    editor->SetBottomOverlayRows(bottom_overlay_rows);
    
    Element top_menu_element = menu_bar_->Render();
        
    Element workspace = editor_config_.show_file_explorer
        ? hbox({
              sidebar_panel_->Render() | size(WIDTH, EQUAL, 28),
              separator(),
              text_editor_->Render() | xflex,
          })
        : hbox({
              text_editor_->Render() | xflex,
          });

    Elements base_rows = {
        text(" textlt v1.0.0 - Native Non-Modal Text Editor") |
            bold |
            color(current_theme_.menu_foreground),
        separator(),
        top_menu_element,
        separator(),
        workspace | yflex,
    };

    if (current_search_mode_ != SearchMode::None) {
        base_rows.push_back(separator());
        base_rows.push_back(RenderFindPanel());
    }
    if (show_goto_line_bar_) {
        base_rows.push_back(separator());
        base_rows.push_back(RenderGoToLinePanel());
    }

    base_rows.push_back(separator());
    const int cursor_row_index = editor->GetCursorRow();
    const int cursor_row = cursor_row_index + 1;
    const int cursor_col = editor->GetCursorCol() + 1;
    const size_t total_lines = editor->GetLineCount();
    int document_percent = 100;
    if (total_lines > 1) {
        document_percent =
            (cursor_row_index * 100) / static_cast<int>(total_lines - 1);
    }
    const std::string file_path = editor->CurrentFilePath();
    const std::string filename =
        std::filesystem::path(file_path).filename().string();
    const auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    const std::filesystem::path git_workspace = editor_config_.show_file_explorer
        ? sidebar->CurrentPath()
        : std::filesystem::path(file_path).parent_path();
    git_manager_.SetWorkingDirectory(
        git_workspace.empty() ? std::filesystem::current_path() : git_workspace);
    const std::string git_branch = git_manager_.GetCurrentBranch();
    const std::string git_branch_badge =
        git_branch.empty() ? "" : " | branch: " + git_branch;
    std::string display_name = filename.empty() ? file_path : filename;
    if (editor->IsDirty()) {
        display_name += " *";
    }

    auto doc = editor->GetDocument();
    std::string type_label = doc ? doc->Label() : "Plain Text";

    base_rows.push_back(
        text(" File: " + display_name +
        " | File Type: " + type_label +
        " | Ln " + std::to_string(cursor_row) + ", Col " + std::to_string(cursor_col) +
        " | " + editor->ActiveLineEndingLabel() +
        git_branch_badge +
        " | " + std::to_string(document_percent) + "%" +
        " | Theme: " + current_theme_.name) |
        color(current_theme_.menu_foreground));

    Element base_layout = vbox(std::move(base_rows)) |
        bgcolor(current_theme_.background);

    Elements layers = {base_layout};
    if (menu_bar_->IsDropdownOpen()) {
        layers.push_back(menu_bar_->RenderDropdown());
    }

    if (file_dialog_.IsOpen()) {
        layers.push_back(file_dialog_.View()->Render() | clear_under | center);
    }
    if (help_dialog_.IsOpen()) {
        layers.push_back(help_dialog_.View()->Render() | clear_under | center);
    }
    if (theme_dialog_.IsOpen()) {
        layers.push_back(theme_dialog_.View()->Render() | clear_under | center);
    }
    if (exit_confirmation_open_) {
        layers.push_back(RenderExitConfirmationDialog() | clear_under | center);
    }

    return dbox(std::move(layers));
}

ftxui::Element TextltApp::RenderFindPanel() {
    using namespace ftxui;

    Element input_column = emptyElement();
    Element action_column = emptyElement();
    std::string mode = "Find";
    if (current_search_mode_ == SearchMode::Replace) {
        mode = "Replace";
        input_column = vbox({
            hbox({
                text(" Find:    "),
                replace_find_input_->Render() | size(WIDTH, GREATER_THAN, 34) | xflex,
            }),
            hbox({
                text(" Replace: "),
                replace_input_->Render() | size(WIDTH, GREATER_THAN, 34) | xflex,
            }),
        });
        action_column = vbox({
            replace_next_button_->Render(),
            replace_all_button_->Render(),
        });
    } else {
        input_column = vbox({
            hbox({
                text(" Find:    "),
                find_input_->Render() | size(WIDTH, GREATER_THAN, 42) | xflex,
            }),
        });
        action_column = vbox({
            find_next_button_->Render(),
            find_previous_button_->Render(),
        });
    }

    // The search panel is split into three columns so text fields keep most
    // horizontal space while actions and filters remain readable at small widths.
    Element filter_column = vbox({
        search_match_case_checkbox_->Render(),
        search_whole_word_checkbox_->Render(),
    });

    return vbox({
        hbox({
            text(" " + mode + " ") | bold | color(current_theme_.menu_foreground),
            separator(),
            text(" " + FindMatchStatus() + " ") | color(current_theme_.menu_foreground),
            filler(),
            text(" Esc closes ") | dim | color(current_theme_.foreground),
        }),
        separator(),
        hbox({
            input_column | xflex,
            separator(),
            action_column | size(WIDTH, GREATER_THAN, 16),
            separator(),
            filter_column | size(WIDTH, GREATER_THAN, 18),
        }),
    }) |
        border |
        bgcolor(current_theme_.menu_background) |
        color(current_theme_.menu_foreground);
}

ftxui::Element TextltApp::RenderGoToLinePanel() {
    using namespace ftxui;

    return hbox({
        text(" Go to Line: ") | bold | color(current_theme_.menu_foreground),
        goto_line_input_component_->Render() | size(WIDTH, GREATER_THAN, 12) | xflex,
        separator(),
        text(" Enter jumps | Esc closes ") | dim | color(current_theme_.foreground),
    }) |
        border |
        bgcolor(current_theme_.menu_background) |
        color(current_theme_.menu_foreground);
}

ftxui::Element TextltApp::RenderExitConfirmationDialog() {
    using namespace ftxui;

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    const std::string file_path = editor->CurrentFilePath();
    const std::string filename = std::filesystem::path(file_path).filename().string();
    const std::string display_name = filename.empty() ? file_path : filename;

    return vbox({
        text(" Unsaved Changes ") | bold | color(current_theme_.modal_accent) | center,
        separator() | color(current_theme_.modal_border),
        text("Save changes to " + display_name + " before closing?") |
            color(current_theme_.modal_text_color) |
            center,
        separator() | color(current_theme_.modal_border),
        hbox({
            filler(),
            exit_save_button_->Render(),
            text(" "),
            exit_discard_button_->Render(),
            text(" "),
            exit_cancel_button_->Render(),
            filler(),
        }),
    }) |
        border |
        bgcolor(current_theme_.modal_background) |
        color(current_theme_.modal_text_color) |
        size(WIDTH, GREATER_THAN, 52);
}

bool TextltApp::HandleGlobalEvent(ftxui::Event event) {
    const std::string& input = event.input();
    const bool has_input = !input.empty();

    // --- Global Event Filter: Modals and Overlays (highest layer first) ---

    // 1. Exit Confirmation Dialog
    if (exit_confirmation_open_) {
        // Dispatch events to confirmation buttons
        if (exit_save_button_->OnEvent(event) ||
            exit_discard_button_->OnEvent(event) ||
            exit_cancel_button_->OnEvent(event)) {
            return true;
        }
        if (event == ftxui::Event::Escape) {
            CloseExitConfirmationDialog();
            return true;
        }
        return true; // Consume all other events when exit confirmation is open.
    }

    // 2. Theme Dialog
    if (theme_dialog_.IsOpen()) {
        if (theme_dialog_.View()->OnEvent(event)) { // Pass event to the dialog's root component
            return true;
        }
        if (event == ftxui::Event::Escape) {
            CloseThemeDialog();
            return true;
        }
        return true; // Consume all other events.
    }

    // 3. Help Dialog
    if (help_dialog_.IsOpen()) {
        if (help_dialog_.OnEvent(event)) { // HelpDialog has its own OnEvent
            return true;
        }
        if (event == ftxui::Event::Escape) {
            CloseHelpDialog();
            return true;
        }
        return true; // Consume all other events.
    }

    // 4. File Dialog
    if (file_dialog_.IsOpen()) {
        if (file_dialog_.View()->OnEvent(event)) { // Pass event to the dialog's root component
            return true;
        }
        if (event == ftxui::Event::Escape) {
            CloseFileDialog();
            return true;
        }
        return true; // Consume all other events.
    }

    // 5. Menu Bar: keyboard events belong here only while a dropdown is open.
    // Mouse events still go to the menu bar so top-level menu clicks work.
    if ((menu_bar_->IsDropdownOpen() || event.is_mouse()) && menu_bar_->OnEvent(event)) {
        return true;
    }

    // 6. Go-to-Line Panel
    if (show_goto_line_bar_) {
        if (goto_line_input_component_->OnEvent(event)) {
            return true;
        }
        if (event == ftxui::Event::Escape) {
            CloseGoToLinePanel();
            return true;
        }
        if (event == ftxui::Event::Return) {
            SubmitGoToLine();
            return true;
        }
        return true; // Consume all other events.
    }

    // 7. Find/Replace Panel
    if (current_search_mode_ != SearchMode::None) {
        // Events are dispatched to focused input or buttons within the panel.
        if (find_input_->OnEvent(event)) return true;
        if (replace_find_input_->OnEvent(event)) return true;
        if (replace_input_->OnEvent(event)) return true;
        if (find_next_button_->OnEvent(event)) return true;
        if (find_previous_button_->OnEvent(event)) return true;
        if (replace_next_button_->OnEvent(event)) return true;
        if (replace_all_button_->OnEvent(event)) return true;
        if (search_match_case_checkbox_->OnEvent(event)) return true;
        if (search_whole_word_checkbox_->OnEvent(event)) return true;

        if (event == ftxui::Event::Escape) {
            CloseFindPanel();
            return true;
        }
        if (event == ftxui::Event::F3) {
            FindNext();
            return true;
        }
        if (input == "\x1B[13;2u" || input == "\x1B[27;2;13~") { // Shift+F3
            FindPrevious();
            return true;
        }
        return true; // Consume all other events.
    }

    // --- End Global Event Filter ---
    // Events reaching this point are not consumed by any active modal or overlay.

    if (input == "\x1Bw" ||
        input == "\x1BW" ||
        input == "Alt+W" ||
        event == ftxui::Event::Special("Alt+W")) {
        CloseCurrentFile();
        return true;
    }

    const bool editor_is_focused = focused_layer_ == 0 && !sidebar_has_focus_;

    // Editor-specific direct manipulation shortcuts (only if editor is focused)
    if (editor_is_focused) {
        if (IsLineManipulationShortcut(event)) {
            if (text_editor_->OnEvent(event)) {
                screen_.PostEvent(ftxui::Event::Custom);
                return true;
            }
            return false;
        }
        if (IsWordDeleteBackwardShortcut(event)) {
            auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
            editor_ptr->DeleteWordBackward();
            screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }
        if (IsWordDeleteForwardShortcut(event)) {
            auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
            editor_ptr->DeleteWordForward();
            screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }
        // Handle Enter key for new line insertion in the editor
        if (event == ftxui::Event::Return) {
            auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
            editor_ptr->HandleAutoIndentReturn();
            screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }
        // Ctrl+A (Select All)
        if (event == ftxui::Event::Special("\x01") ||
            input == "\x01" ||
            event == ftxui::Event::Special("Ctrl+A") ||
            input == "Ctrl+A") {
            auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
            editor_ptr->SelectAll();
            active_action_ = "Selected all text";
            screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }
        // Ctrl+J (TTS Debug)
        if (event == ftxui::Event::Special("\x0A") ||
            input == "\x0A" ||
            event == ftxui::Event::Special("Ctrl+J") ||
            input == "Ctrl+J") {
            QueueCloudTtsDebugFromCursor();
            return true;
        }

        // Pass all other editor-specific events (characters, navigation, etc.) to the editor.
        if (text_editor_->OnEvent(event)) {
            return true;
        }
    }

    // Global App Shortcuts (available when no modals are active)
    if (event.input() == "\x11") { // Ctrl+Q
        RequestExit();
        return true;
    }
    if (event.input() == "\x13") { // Ctrl+S
        SaveCurrentFile();
        return true;
    }
    if (event.input() == "\x0F") { // Ctrl+O
        OpenFileDialog(FilePromptMode::Open);
        return true;
    }

    // Clipboard shortcuts (trigger dropdown actions)
    if (event == ftxui::Event::Special("\x03") ||
        (has_input && input[0] == '\x03') ||
        event == ftxui::Event::Special("Ctrl+Shift+C") ||
        input == "Ctrl+Shift+C") {
        RunDropdownAction(1, 4); // This will trigger copy.
        screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }
    if (event == ftxui::Event::Special("\x18") ||
        (has_input && input[0] == '\x18') ||
        event == ftxui::Event::Special("Ctrl+Shift+X") ||
        input == "Ctrl+Shift+X") {
        RunDropdownAction(1, 3); // This will trigger cut.
        screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }
    // Keep Ctrl+Shift+V for terminal configurations that reserve Ctrl+V.
    if (input == "\x16" ||
        input == "Ctrl+V" ||
        event == ftxui::Event::Special("Ctrl+Shift+V") ||
        input == "Ctrl+Shift+V") {
        RunDropdownAction(1, 5); // Index for "Paste"
        return true;
    }
    
    // Shortcuts to open Find/Replace/Go-to-Line panels
    if (input == "\x06" || input == "Ctrl+F") {
        OpenFindPanel(false);
        return true;
    }
    if (event == ftxui::Event::Special("\x12") || input == "\x12" || input == "Ctrl+R") {
        OpenFindPanel(true);
        return true;
    }
    if (event == ftxui::Event::Special("\x07") || input == "\x07" || input == "Ctrl+G") {
        OpenGoToLinePanel();
        return true;
    }

    // F-key shortcuts to open main menu dropdowns/dialogs
    if (event == ftxui::Event::F1) {
        OpenHelpDialog();
        return true;
    }
    if (event == ftxui::Event::F2) {
        menu_bar_->OpenDropdown(0);
        focused_layer_ = 0;
        return true;
    }
    if (event == ftxui::Event::F3) {
        menu_bar_->OpenDropdown(1);
        focused_layer_ = 0;
        return true;
    }
    if (event == ftxui::Event::F4) {
        menu_bar_->OpenDropdown(2);
        focused_layer_ = 0;
        return true;
    }
    
    return false;
}

} // namespace textlt
