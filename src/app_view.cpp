#include "app.hpp"

#include <algorithm>
#include <filesystem>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "file_utils.hpp"
#include "theme.hpp"

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

} // namespace

ftxui::Element TextltApp::Render() {
    using namespace ftxui;

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    UpdateFileMenuLabels();
    size_t bottom_overlay_rows = 2; // Status separator + status bar.
    if (current_search_mode_ != SearchMode::None) {
        bottom_overlay_rows += 7; // Find separator plus the bordered multi-row search panel.
    }
    if (show_goto_line_bar_) {
        bottom_overlay_rows += 4; // Go-to-line separator + bordered panel.
    }
    editor->SetBottomOverlayRows(bottom_overlay_rows);
    
    Element top_menu_element =
        top_menu_->Render() |
        bgcolor(current_theme_.menu_background) |
        color(current_theme_.menu_foreground) |
        reflect(top_menu_box_);
        
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
    base_rows.push_back(
        text(" File: " + display_name +
             " | File Type: " + FileTypeLabel(file_path) +
             " | Ln " + std::to_string(cursor_row) + ", Col " + std::to_string(cursor_col) +
             " | " + editor->ActiveLineEndingLabel() +
             git_branch_badge +
             " | " + std::to_string(document_percent) + "%" +
             " | Theme: " + current_theme_.name) |
            color(current_theme_.menu_foreground));

    Element base_layout = vbox(std::move(base_rows)) |
        bgcolor(current_theme_.background) |
        color(current_theme_.foreground);

    if (active_dropdown_ < 0 && !file_dialog_.IsOpen() &&
        !help_dialog_.IsOpen() && !theme_dialog_.IsOpen() &&
        !exit_confirmation_open_) {
        return base_layout;
    }

    Elements layers = {base_layout};
    if (active_dropdown_ >= 0) {
        layers.push_back(vbox({
            filler() | size(HEIGHT, EQUAL, top_menu_box_.y_max + 1),
            hbox({
                filler() | size(WIDTH, EQUAL, std::max(0, DropdownX())),
                dropdown_menu_->Render() | 
                    border | 
                    bgcolor(current_theme_.menu_background) | 
                    color(current_theme_.menu_foreground) | 
                    clear_under,
                filler(),
            }),
            filler(),
        }));
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

    if (exit_confirmation_open_) {
        if (event == ftxui::Event::Escape) {
            CloseExitConfirmationDialog();
            return true;
        }
        return false;
    }

    if (show_goto_line_bar_) {
        if (event == ftxui::Event::Escape) {
            CloseGoToLinePanel();
            return true;
        }
        if (event == ftxui::Event::Return) {
            SubmitGoToLine();
            return true;
        }
    }

    if (current_search_mode_ != SearchMode::None) {
        if (event == ftxui::Event::Escape) {
            CloseFindPanel();
            return true;
        }
        if (event == ftxui::Event::F3) {
            FindNext();
            return true;
        }
        if (input == "\x1B[13;2u" || input == "\x1B[27;2;13~") {
            FindPrevious();
            return true;
        }
    }

    const bool can_open_find_panel =
        !file_dialog_.IsOpen() && !help_dialog_.IsOpen() && !theme_dialog_.IsOpen() &&
        !exit_confirmation_open_;

    const bool editor_can_handle_direct_shortcut =
        focused_layer_ == 0 &&
        active_dropdown_ < 0 &&
        !sidebar_has_focus_ &&
        !file_dialog_.IsOpen() &&
        !help_dialog_.IsOpen() &&
        !theme_dialog_.IsOpen() &&
        current_search_mode_ == SearchMode::None &&
        !show_goto_line_bar_;
    const bool word_delete_shortcut =
        input == "\x17" ||
        input == "Ctrl+Backspace" ||
        input == "Ctrl+Delete" ||
        input == "\x1B[127;5u" ||
        input == "\x1B[8;5u" ||
        input == "\x1B[27;5;127~" ||
        input == "\x1B[27;5;8~" ||
        input == "\x1B[3;5~" ||
        input == "\x1B[3;5u";
    if (editor_can_handle_direct_shortcut && IsLineManipulationShortcut(event)) {
        if (text_editor_->OnEvent(event)) {
            screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }
        return false;
    }
    if (editor_can_handle_direct_shortcut && word_delete_shortcut) {
        text_editor_->OnEvent(event);
        screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }

    if (can_open_find_panel && (input == "\x06" || input == "Ctrl+F")) {
        OpenFindPanel(false);
        return true;
    }

    // Ctrl+H is indistinguishable from Backspace in many terminals, so Replace
    // uses Ctrl+R to avoid stealing destructive editing input.
    if (can_open_find_panel &&
        (event == ftxui::Event::Special("\x12") || input == "\x12" || input == "Ctrl+R")) {
        OpenFindPanel(true);
        return true;
    }

    if (can_open_find_panel &&
        (event == ftxui::Event::Special("\x07") || input == "\x07" || input == "Ctrl+G")) {
        OpenGoToLinePanel();
        return true;
    }

    if (editor_can_handle_direct_shortcut &&
        (event == ftxui::Event::Special("\x0A") ||
         input == "\x0A" ||
         event == ftxui::Event::Special("Ctrl+J") ||
         input == "Ctrl+J")) {
        QueueCloudTtsDebugFromCursor();
        return true;
    }

    if (event == ftxui::Event::Special("\x03") ||
        (has_input && input[0] == '\x03') ||
        event == ftxui::Event::Special("Ctrl+Shift+C") ||
        input == "Ctrl+Shift+C") {
        selected_menu_item_ = 1;
        active_dropdown_ = 1;
        selected_dropdown_item_ = 3;
        RunDropdownAction();
        screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }

    if (event == ftxui::Event::Special("\x18") ||
        (has_input && input[0] == '\x18') ||
        event == ftxui::Event::Special("Ctrl+Shift+X") ||
        input == "Ctrl+Shift+X") {
        selected_menu_item_ = 1;
        active_dropdown_ = 1;
        selected_dropdown_item_ = 2;
        RunDropdownAction();
        screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }
    
    // Keep Ctrl+Shift+V for terminal configurations that reserve Ctrl+V.
    if (input == "\x16" ||
        input == "Ctrl+V" ||
        event == ftxui::Event::Special("Ctrl+Shift+V") ||
        input == "Ctrl+Shift+V") {
        active_dropdown_ = 1;
        selected_dropdown_item_ = 4; // Index for "Paste"
        RunDropdownAction();
        return true;
    }
    
    if (event.input() == "\x11") {
        RequestExit();
        return true;
    }
    // Continue with the remaining global shortcut handlers.

    if (event == ftxui::Event::Escape && help_dialog_.IsOpen()) {
        CloseHelpDialog();
        return true;
    }
    if (help_dialog_.IsOpen()) {
        help_dialog_.OnEvent(event);
        return true;
    }

    if (event == ftxui::Event::Escape && theme_dialog_.IsOpen()) {
        CloseThemeDialog();
        return true;
    }
    if (theme_dialog_.IsOpen()) {
        return false;
    }

    if (event.input() == "\x13" && !file_dialog_.IsOpen()) {
        SaveCurrentFile();
        return true;
    }
    if (event.input() == "\x0F" && !file_dialog_.IsOpen()) {
        OpenFileDialog(FilePromptMode::Open);
        return true;
    }

    if (event == ftxui::Event::Escape && file_dialog_.IsOpen()) {
        CloseFileDialog();
        return true;
    }
    if (file_dialog_.IsOpen()) {
        return false;
    }

    if (event == ftxui::Event::Escape && active_dropdown_ >= 0) {
        CloseDropdown();
        return true;
    }
    if (event == ftxui::Event::ArrowLeft && active_dropdown_ >= 0) {
        selected_menu_item_ =
            (selected_menu_item_ + static_cast<int>(menu_entries_.size()) - 1) %
            static_cast<int>(menu_entries_.size());
        ActivateTopMenu();
        return true;
    }
    if (event == ftxui::Event::ArrowRight && active_dropdown_ >= 0) {
        selected_menu_item_ =
            (selected_menu_item_ + 1) % static_cast<int>(menu_entries_.size());
        ActivateTopMenu();
        return true;
    }

    if (event == ftxui::Event::F1) {
        selected_menu_item_ = 3;
        OpenHelpDialog();
        return true;
    }
    if (event == ftxui::Event::F2) {
        selected_menu_item_ = 0;
        OpenDropdown();
        return true;
    }
    if (event == ftxui::Event::F3) {
        selected_menu_item_ = 1;
        OpenDropdown();
        return true;
    }
    if (event == ftxui::Event::F4) {
        selected_menu_item_ = 2;
        OpenDropdown();
        return true;
    }

    // Process initial left mouse click down on the top menu bar items
    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed &&
        top_menu_box_.Contain(event.mouse().x, event.mouse().y)) {
        int item_x = top_menu_box_.x_min;
        for (size_t i = 0; i < menu_entries_.size(); ++i) {
            const int item_width = static_cast<int>(menu_entries_[i].size());
            if (event.mouse().x >= item_x && event.mouse().x < item_x + item_width) {
                selected_menu_item_ = static_cast<int>(i);
                ActivateTopMenu();
                return true;
            }
            item_x += item_width + 1;
        }
    }

    if (active_dropdown_ >= 0 && event.is_mouse() && 
        event.mouse().button == ftxui::Mouse::Left && 
        event.mouse().motion == ftxui::Mouse::Released) {
        
        // Target rendering geometry boundary checks
        int min_x = std::max(0, DropdownX()) + 1; // Includes left border line offset
        int min_y = top_menu_box_.y_max + 2;     // Lines layout offset below the top border
        
        int clicked_y = event.mouse().y - min_y;
        int clicked_x = event.mouse().x - min_x;

        // Check if the coordinates physically hit inside the boundaries of the dropdown layout box
        if (clicked_y >= 0 && clicked_y < static_cast<int>(current_dropdown_entries_.size()) && clicked_x >= 0) {
            selected_dropdown_item_ = clicked_y;
            RunDropdownAction();
            return true;
        }
    }

    return false;
}

int TextltApp::DropdownX() const {
    int dropdown_x = top_menu_box_.x_min;
    for (int i = 0; i < active_dropdown_; ++i) {
        dropdown_x += static_cast<int>(menu_entries_[i].size()) + 1;
    }
    return dropdown_x;
}

} // namespace textlt
