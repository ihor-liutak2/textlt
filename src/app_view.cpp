#include "app.hpp"

#include <algorithm>
#include <filesystem>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include <ftxui/component/component_base.hpp>
#include "file_utils.hpp"
#include "keyboard_shortcuts.hpp"
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

bool IsAltHShortcut(const ftxui::Event& event) {
    return MatchesShortcut(event, ShortcutModifier::Alt, 'h');
}

bool IsAltJShortcut(const ftxui::Event& event) {
    return MatchesShortcut(event, ShortcutModifier::Alt, 'j');
}

bool IsAltSShortcut(const ftxui::Event& event) {
    return MatchesShortcut(event, ShortcutModifier::Alt, 's');
}

bool IsCtrlBShortcut(const ftxui::Event& event) {
    return MatchesShortcut(event, ShortcutModifier::Ctrl, 'b');
}

bool IsAltBShortcut(const ftxui::Event& event) {
    return MatchesShortcut(event, ShortcutModifier::Alt, 'b');
}

bool IsOpenedSidebarChordKey(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "o" ||
        input == "O" ||
        input == "\x0F" ||
        input == "Ctrl+O" ||
        event == ftxui::Event::Special("Ctrl+O");
}

} // namespace

ftxui::Element TextltApp::Render() {
    using namespace ftxui;

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    UpdateFileMenuLabels();
    RefreshOpenedDocumentsSidebar();

    Element top_menu_element = menu_bar_->Render();
        
    Element editor_workspace = editor_workspace_container_
        ? editor_workspace_container_->Render() | xflex
        : text_editor_->Render() | xflex;

    Element workspace = editor_config_.show_file_explorer
        ? hbox({
              sidebar_panel_->Render() | size(WIDTH, EQUAL, 28),
              separator(),
              editor_workspace | xflex,
          })
        : hbox({
              editor_workspace | xflex,
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

    if (help_dialog_.IsOpen()) {
        layers.push_back(help_dialog_.View()->Render() | clear_under | center);
    }
    if (recent_files_modal_.IsOpen()) {
        layers.push_back(recent_files_modal_.View()->Render() | clear_under | center);
    }
    if (search_files_modal_.IsOpen()) {
        layers.push_back(search_files_modal_.View()->Render() | clear_under | center);
    }
    if (files_modal_.IsOpen()) {
        layers.push_back(files_modal_.View()->Render() | clear_under | center);
    }
    if (text_processors_modal_.IsOpen()) {
        layers.push_back(text_processors_modal_.View()->Render() | clear_under | center);
    }
    if (git_modal_.IsOpen()) {
        layers.push_back(git_modal_.View()->Render() | clear_under | center);
    }
    if (git_settings_modal_.IsOpen()) {
        layers.push_back(git_settings_modal_.View()->Render() | clear_under | center);
    }
    if (theme_dialog_.IsOpen()) {
        layers.push_back(theme_dialog_.View()->Render() | clear_under | center);
    }
    if (tts_modal_.IsOpen()) {
        layers.push_back(tts_modal_.View()->Render() | clear_under | center);
    }
    if (view_layout_modal_.IsOpen()) {
        layers.push_back(view_layout_modal_.View()->Render() | clear_under | center);
    }
    if (ai_actions_modal_.IsOpen()) {
        layers.push_back(ai_actions_modal_.View()->Render() | clear_under | center);
    }
    if (assistant_settings_modal_.IsOpen()) {
        layers.push_back(assistant_settings_modal_.View()->Render() | clear_under | center);
    }
    if (unsaved_changes_dialog_.IsOpen()) {
        layers.push_back(unsaved_changes_dialog_.View()->Render() | clear_under | center);
    }

    return dbox(std::move(layers));
}

ftxui::Element TextltApp::RenderFindPanel() {
    using namespace ftxui;

    Element input_column = emptyElement();
    Element utility_column = emptyElement();
    Element action_column = emptyElement();
    std::string mode = "Find";
    if (current_search_mode_ == SearchMode::Replace) {
        mode = "Replace";
        input_column = vbox({
            hbox({
                text(" Find:    "),
                replace_find_input_->Render() | size(WIDTH, GREATER_THAN, 34) | xflex,
            }),
            emptyElement(),
            hbox({
                text(" Replace: "),
                replace_input_->Render() | size(WIDTH, GREATER_THAN, 34) | xflex,
            }),
        });
        utility_column = vbox({
            replace_paste_button_->Render(),
            replace_clear_button_->Render(),
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
        utility_column = vbox({
            find_paste_button_->Render(),
            find_clear_button_->Render(),
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
        hbox({
            input_column | xflex,
            separator(),
            utility_column | size(WIDTH, GREATER_THAN, 9),
            text(" "),
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

void TextltApp::SetActiveLayer(UiLayer layer) {
    active_layer_ = layer;
    active_layer_index_ = static_cast<int>(layer);
}

TextltApp::UiLayer TextltApp::ActiveLayer() const {
    return active_layer_;
}

bool TextltApp::ActiveModalIsOpen() const {
    switch (ActiveLayer()) {
        case UiLayer::Main: return true;
        case UiLayer::Help: return help_dialog_.IsOpen();
        case UiLayer::Theme: return theme_dialog_.IsOpen();
        case UiLayer::Find: return current_search_mode_ != SearchMode::None;
        case UiLayer::GoToLine: return show_goto_line_bar_;
        case UiLayer::UnsavedChanges: return unsaved_changes_dialog_.IsOpen();
        case UiLayer::RecentFiles: return recent_files_modal_.IsOpen();
        case UiLayer::SearchFiles: return search_files_modal_.IsOpen();
        case UiLayer::Files: return files_modal_.IsOpen();
        case UiLayer::TextProcessors: return text_processors_modal_.IsOpen();
        case UiLayer::Git: return git_modal_.IsOpen();
        case UiLayer::GitSettings: return git_settings_modal_.IsOpen();
        case UiLayer::Tts: return tts_modal_.IsOpen();
        case UiLayer::ViewLayout: return view_layout_modal_.IsOpen();
        case UiLayer::AiActions: return ai_actions_modal_.IsOpen();
        case UiLayer::AssistantSettings: return assistant_settings_modal_.IsOpen();
    }
    return false;
}

bool TextltApp::HandleGlobalEvent(ftxui::Event event) {
    const std::string& input = event.input();

    // Modal content is selected by Container::Tab and receives keyboard,
    // focus and mouse events through the FTXUI component tree. If a modal
    // closed itself from one of its controls, restore the main layer before
    // handling the next event.
    if (ActiveLayer() != UiLayer::Main && !ActiveModalIsOpen()) {
        FocusEditor();
    }

    if (ActiveLayer() == UiLayer::GoToLine) {
        if (event == ftxui::Event::Escape) {
            CloseGoToLinePanel();
            return true;
        }
        return false;
    }

    if (ActiveLayer() == UiLayer::Find) {
        if (find_input_->Focused() || replace_find_input_->Focused()) {
            active_search_panel_input_ = SearchPanelInput::Find;
        } else if (replace_input_->Focused()) {
            active_search_panel_input_ = SearchPanelInput::Replace;
        }
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
        return false;
    }

    if (ActiveLayer() != UiLayer::Main) {
        return false;
    }

    // Let FTXUI route menu events through the component tree. Calling
    // MenuBarComponent::OnEvent directly from this outer CatchEvent changes
    // layers in the middle of a mouse sequence when an item opens a modal.
    if (menu_bar_->IsDropdownOpen()) {
        return event.is_mouse() ? false : menu_bar_->OnEvent(event);
    }

    // Mouse input on the main screen is routed exclusively by FTXUI through
    // root_container_. Manual dispatch here competes with Container hit-testing
    // and can consume a menu click before it reaches MenuBarComponent.
    if (event.is_mouse()) {
        return false;
    }

    if (IsAltJShortcut(event)) {
        OpenAiActionsModal();
        return true;
    }
    if (IsAltSShortcut(event)) {
        OpenAssistantSettingsModal();
        return true;
    }

    if (MatchesShortcut(event, ShortcutModifier::Alt, 'w') ||
        MatchesShortcut(event, ShortcutModifier::Ctrl, 'w')) {
        CloseCurrentFile();
        return true;
    }

    if (pending_sidebar_chord_) {
        pending_sidebar_chord_ = false;
        if (IsOpenedSidebarChordKey(event)) {
            ShowOpenedFilesSidebar();
            return true;
        }
    }

    if (IsAltBShortcut(event)) {
        pending_sidebar_chord_ = false;
        ToggleSidebarOpenedProject();
        return true;
    }

    if (IsCtrlBShortcut(event)) {
        pending_sidebar_chord_ = true;
        HandleCtrlBFileExplorer();
        return true;
    }

    const bool sidebar_is_focused = ActiveLayer() == UiLayer::Main && sidebar_has_focus_;
    if (sidebar_is_focused) {
        if (event == ftxui::Event::Escape) {
            FocusEditor();
            return true;
        }
        if (event == ftxui::Event::ArrowUp ||
            event == ftxui::Event::ArrowDown ||
            event == ftxui::Event::Return ||
            input == "\x0A") {
            const ftxui::Event sidebar_event =
                input == "\x0A" ? ftxui::Event::Return : event;
            if (sidebar_panel_->OnEvent(sidebar_event)) {
                screen_.PostEvent(ftxui::Event::Custom);
                return true;
            }
        }
    }

    if (IsAltHShortcut(event)) {
        OpenTtsModal();
        return true;
    }

    const bool editor_is_focused = ActiveLayer() == UiLayer::Main && !sidebar_has_focus_;

    // Editor-specific direct manipulation shortcuts (only if editor is focused)
    if (editor_is_focused) {
        if (HandleTerminalBracketedPaste(event)) {
            return true;
        }

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
        if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'a')) {
            auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
            editor_ptr->SelectAll();
            active_action_ = "Selected all text";
            screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }
        // Pass all other editor-specific events (characters, navigation, etc.) to the editor.
        if (text_editor_->OnEvent(event)) {
            return true;
        }
    }

    // Global App Shortcuts (available when no modals are active)
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'q')) {
        RequestExit();
        return true;
    }
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 's')) {
        SaveCurrentFile();
        return true;
    }
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'o')) {
        OpenFilesModal(FilesModalMode::Open);
        return true;
    }

    // Clipboard shortcuts (trigger dropdown actions)
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'c') ||
        event == ftxui::Event::Special("Ctrl+Shift+C") ||
        input == "Ctrl+Shift+C") {
        RunDropdownAction(1, 4); // This will trigger copy.
        screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'x') ||
        event == ftxui::Event::Special("Ctrl+Shift+X") ||
        input == "Ctrl+Shift+X") {
        RunDropdownAction(1, 3); // This will trigger cut.
        screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }
    // Keep Ctrl+Shift+V for terminal configurations that reserve Ctrl+V.
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'v') ||
        event == ftxui::Event::Special("Ctrl+Shift+V") ||
        input == "Ctrl+Shift+V") {
        RunDropdownAction(1, 5); // Index for "Paste"
        return true;
    }
    
    // Shortcuts to open Find/Replace/Go-to-Line panels
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'f')) {
        OpenFindPanel(false);
        return true;
    }
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'r')) {
        OpenFindPanel(true);
        return true;
    }
    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'g')) {
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
        SetActiveLayer(UiLayer::Main);
        return true;
    }
    if (event == ftxui::Event::F3) {
        menu_bar_->OpenDropdown(1);
        SetActiveLayer(UiLayer::Main);
        return true;
    }
    if (event == ftxui::Event::F4) {
        menu_bar_->OpenDropdown(2);
        SetActiveLayer(UiLayer::Main);
        return true;
    }
    
    return false;
}

} // namespace textlt
