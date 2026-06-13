#include "app.hpp"

#include <algorithm>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "file_utils.hpp"
#include "theme.hpp"

namespace textlt {

ftxui::Element TextltApp::Render() {
    using namespace ftxui;

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    
    Element top_menu_element =
        top_menu_->Render() |
        bgcolor(current_theme_.menu_background) |
        color(current_theme_.menu_foreground) |
        reflect(top_menu_box_);
        
    Element workspace = editor_config_.show_file_explorer
        ? hbox({
              file_explorer_->Render() | size(WIDTH, EQUAL, 28),
              separator(),
              text_editor_->Render() | xflex,
          })
        : hbox({
              text_editor_->Render() | xflex,
          });

    Elements base_rows = {
        text(" textlt v0.1.0 - Native Non-Modal Text Editor") |
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
    const int cursor_row = editor->GetCursorRow() + 1;
    const int cursor_col = editor->GetCursorCol() + 1;
    base_rows.push_back(
        text(" Active Action: " + active_action_ +
             " | File: " + editor->CurrentFilePath() +
             " | File Type: " + FileTypeLabel(editor->CurrentFilePath()) +
             " | Ln " + std::to_string(cursor_row) + ", Col " + std::to_string(cursor_col) +
             " | Theme: " + current_theme_.name) |
            color(current_theme_.menu_foreground));

    Element base_layout = vbox(std::move(base_rows)) |
        bgcolor(current_theme_.background) |
        color(current_theme_.foreground);

    if (active_dropdown_ < 0 && !file_dialog_.IsOpen() &&
        !help_dialog_.IsOpen() && !theme_dialog_.IsOpen()) {
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

    return dbox(std::move(layers));
}

ftxui::Element TextltApp::RenderFindPanel() {
    using namespace ftxui;

    Element controls = emptyElement();
    std::string mode = " Find ";
    if (current_search_mode_ == SearchMode::Replace) {
        mode = " Replace ";
        controls = hbox({
            text(" Find: "),
            replace_find_input_->Render() | size(WIDTH, GREATER_THAN, 20) | xflex,
            text(" Replace: "),
            replace_input_->Render() | size(WIDTH, GREATER_THAN, 20) | xflex,
            separator(),
            replace_next_button_->Render(),
            replace_all_button_->Render(),
        });
    } else {
        controls = hbox({
            text(" Find: "),
            find_input_->Render() | size(WIDTH, GREATER_THAN, 28) | xflex,
            separator(),
            find_next_button_->Render(),
            find_previous_button_->Render(),
        });
    }

    return hbox({
        text(mode) | bold | color(current_theme_.menu_foreground),
        separator(),
        controls | xflex,
        separator(),
        text(" " + FindMatchStatus() + " ") | color(current_theme_.menu_foreground),
        text(" Esc closes ") | dim | color(current_theme_.foreground),
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

bool TextltApp::HandleGlobalEvent(ftxui::Event event) {
    const std::string& input = event.input();
    const bool has_input = !input.empty();

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
        !file_dialog_.IsOpen() && !help_dialog_.IsOpen() && !theme_dialog_.IsOpen();

    if (can_open_find_panel && (input == "\x06" || input == "Ctrl+F")) {
        OpenFindPanel(false);
        return true;
    }

    if (can_open_find_panel && (input == "\x08" || input == "Ctrl+H")) {
        OpenFindPanel(true);
        return true;
    }

    if (can_open_find_panel &&
        (event == ftxui::Event::Special("\x07") || input == "\x07" || input == "Ctrl+G")) {
        OpenGoToLinePanel();
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
    
    // 3. Optional: Intercept pure Ctrl+Shift+V only if your Konsole bypasses it.
    // Otherwise, Konsole will just stream characters automatically into EditorComponent::OnEvent
    if (event == ftxui::Event::Special("Ctrl+Shift+V") || event.input() == "Ctrl+Shift+V") {
        active_dropdown_ = 1;
        selected_dropdown_item_ = 4; // Index for "Paste"
        RunDropdownAction();
        return true;
    }
    
    if (event.input() == "\x11") {
        screen_.Exit();
        return true;
    }
    // ... далі весь твій код без змін ...

    if (event == ftxui::Event::Escape && help_dialog_.IsOpen()) {
        CloseHelpDialog();
        return true;
    }
    if (help_dialog_.IsOpen()) {
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
