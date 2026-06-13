#include "app.hpp"

#include <stdexcept>

#include "ftxui/component/component_options.hpp"
#include "theme.hpp"

namespace textlt {

TextltApp::TextltApp()
    : config_manager_("config.json"),
      editor_config_(config_manager_.Load()),
      themes_(LoadThemesFromDirectory("themes")),
      current_theme_(FindThemeByName(themes_, editor_config_.active_theme_name)),
      screen_(ftxui::ScreenInteractive::Fullscreen()),
      text_editor_(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_)),
      file_explorer_(ftxui::Make<FileExplorer>(
          [this](const std::filesystem::path& path) { OpenExplorerFile(path); })),
      file_dialog_(&current_theme_, [this](
          FilePromptMode mode,
          const std::string& path,
          std::string& error) {
          return ConfirmFileDialog(mode, path, error);
      }),
      help_dialog_(&current_theme_),
      theme_dialog_(&current_theme_, [this](const std::string& theme_name) {
          SelectTheme(theme_name);
      }),
      menu_entries_({
          " File ",
          " Edit ",
          " Options ",
          " Help ",
          " Exit ",
      }),
      dropdown_entries_({
          {" New ", " Open ", " Save ", " Save As ", " Exit "},
          {" Undo ", " Redo ", " Cut ", " Copy ", " Paste "},
          {
              " Toggle Line Numbers ",
              " Toggle File Explorer ",
              " Theme... ",
          },
          {" About textlt ", " Keyboard Shortcuts "},
          {" Exit "},
      }),
      current_dropdown_entries_(dropdown_entries_[0]) {
    editor_config_.active_theme_name = current_theme_.name;

    ftxui::MenuOption top_menu_option = ftxui::MenuOption::Toggle();
    top_menu_option.on_enter = [this] { ActivateTopMenu(); };
    top_menu_option.on_change = [this] { ActivateTopMenu(); };
    
    top_menu_ = ftxui::Menu(&menu_entries_, &selected_menu_item_, top_menu_option);

    ftxui::MenuOption dropdown_option = ftxui::MenuOption::Vertical();
    dropdown_option.on_enter = [this] { RunDropdownAction(); };
    
    dropdown_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        ftxui::Element item = ftxui::text(state.label);
        
        // Apply theme colors for highlighted items safely
        if (state.focused || state.active) {
            return item | ftxui::bgcolor(current_theme_.menu_foreground) 
                        | ftxui::color(current_theme_.menu_background);
        }
        return item;
    };

    dropdown_menu_ = ftxui::Menu(
        &current_dropdown_entries_, &selected_dropdown_item_, dropdown_option);

    body_container_ = ftxui::Container::Horizontal({
        file_explorer_,
        text_editor_,
    });

    // FIXED: Added missing underscore to match class declaration
    main_container_ = ftxui::Container::Vertical({
        top_menu_,
        body_container_,
    });

    root_container_ = ftxui::Container::Tab({
        main_container_,
        dropdown_menu_,
        file_dialog_.View(),
        help_dialog_.View(),
        theme_dialog_.View(),
    }, &focused_layer_);

    renderer_ = ftxui::Renderer(root_container_, [this] { return Render(); });
    global_shortcuts_ = ftxui::CatchEvent(
        renderer_, [this](ftxui::Event event) { return HandleGlobalEvent(event); });

    FocusEditor();
}

void TextltApp::Run() {
    screen_.ForceHandleCtrlC(false);
    screen_.Loop(global_shortcuts_);
}

void TextltApp::CloseDropdown() {
    active_dropdown_ = -1;
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenDropdown() {
    // FIXED: Added missing trailing underscore
    active_dropdown_ = selected_menu_item_;
    selected_dropdown_item_ = 0;
    current_dropdown_entries_ = dropdown_entries_[active_dropdown_];
    focused_layer_ = 1;
    dropdown_menu_->TakeFocus();
}

void TextltApp::CloseFileDialog() {
    file_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenFileDialog(FilePromptMode mode) {
    active_dropdown_ = -1;
    focused_layer_ = 2;
    
    std::string default_path = std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
    
    // If the active file has no path or is an untitled draft, sync with the File Explorer selection
    if (default_path.empty() || default_path == "untitled.txt") {
        auto explorer_ptr = std::static_pointer_cast<FileExplorer>(file_explorer_);
        
        // Dynamic path acquisition directly from the highlighted file tree node element
        default_path = explorer_ptr->GetSelectedDirectoryPath().string();
        
        // Append trailing slash so the path is prepared for filename entry instantly
        if (!default_path.empty() && default_path.back() != '/') {
            default_path += "/";
        }
    }
    
    file_dialog_.Open(mode, default_path);
}

void TextltApp::OpenHelpDialog() {
    active_dropdown_ = -1;
    help_dialog_.Open("help.txt");
    active_action_ = "Opened Help";
    focused_layer_ = 3;
}

void TextltApp::CloseHelpDialog() {
    help_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenThemeDialog() {
    active_dropdown_ = -1;
    focused_layer_ = 4;
    theme_dialog_.Open(themes_, editor_config_.active_theme_name);
}

void TextltApp::CloseThemeDialog() {
    theme_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::ActivateTopMenu() {
    if (selected_menu_item_ == 3) {
        OpenHelpDialog();
        return;
    }
    OpenDropdown();
}

void TextltApp::SwitchEditorFocus() {
    if (!editor_config_.show_file_explorer) {
        FocusEditor();
        return;
    }
    if (explorer_has_focus_) {
        FocusEditor();
    } else {
        FocusExplorer();
    }
}

void TextltApp::FocusEditor() {
    explorer_has_focus_ = false;
    focused_layer_ = 0;
    text_editor_->TakeFocus();
}

void TextltApp::FocusExplorer() {
    explorer_has_focus_ = true;
    focused_layer_ = 0;
    std::static_pointer_cast<FileExplorer>(file_explorer_)->FocusMenu();
}

bool TextltApp::SaveFile(const std::string& path, std::string& error) {
    try {
        std::static_pointer_cast<EditorComponent>(text_editor_)->SaveToFile(path);
        active_action_ = "Saved " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}

bool TextltApp::OpenFile(const std::string& path, std::string& error) {
    try {
        std::static_pointer_cast<EditorComponent>(text_editor_)->LoadFromFile(path);
        active_action_ = "Opened " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}

void TextltApp::OpenExplorerFile(const std::filesystem::path& path) {
    std::string error;
    if (OpenFile(path.string(), error)) {
        FocusEditor();
    }
}

void TextltApp::SaveCurrentFile() {
    const std::string& current_path =
        std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
    if (current_path.empty() || current_path == "untitled.txt") {
        OpenFileDialog(FilePromptMode::SaveAs);
        return;
    }

    std::string error;
    SaveFile(current_path, error);
}

bool TextltApp::ConfirmFileDialog(
    FilePromptMode mode,
    const std::string& path,
    std::string& error) {
    if (mode == FilePromptMode::Open) {
        return OpenFile(path, error);
    }
    if (mode == FilePromptMode::SaveAs) {
        return SaveFile(path, error);
    }
    error = "No file action selected.";
    return false;
}

void TextltApp::SelectTheme(const std::string& theme_name) {
    current_theme_ = FindThemeByName(themes_, theme_name);
    editor_config_.active_theme_name = current_theme_.name;
    active_action_ = "Theme changed to " + current_theme_.name;
    SaveConfig();
    CloseThemeDialog();
}

void TextltApp::SaveConfig() {
    config_manager_.Save(editor_config_);
}

void TextltApp::RunDropdownAction() {
    // Generate simple trace actions for debugging inside the app status-bar
    active_action_ = "DEBUG: Menu=" + std::to_string(active_dropdown_) + 
                    " Item=" + std::to_string(selected_dropdown_item_);

    // Sub-route requests explicitly based on the active dropdown catalog index
    switch (active_dropdown_) {
        case 0: HandleFileMenu(selected_dropdown_item_);     return;
        case 1: HandleEditMenu(selected_dropdown_item_);     return;
        case 2: HandleOptionsMenu(selected_dropdown_item_);  return;
        case 3: OpenHelpDialog();                            return;
        case 4: if (selected_dropdown_item_ == 0) screen_.Exit(); return;
        default: CloseDropdown();                            return;
    }
}
    
    void TextltApp::HandleFileMenu(int item) {
    if (item == 1) { OpenFileDialog(FilePromptMode::Open); return; }
    if (item == 2) { SaveCurrentFile(); return; }
    if (item == 3) { OpenFileDialog(FilePromptMode::SaveAs); return; }
    if (item == 4) { screen_.Exit(); return; }
    CloseDropdown();
}

void TextltApp::HandleEditMenu(int item) {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

    if (item == 2) { // Cut
        FocusEditor();
        if (editor_ptr->HasSelection()) {
            // Priority 1: Cut only the user's active Shift-selection
            std::string selected_text = editor_ptr->GetSelectedText();
            WriteSystemClipboard(selected_text);
            editor_ptr->DeleteSelection();
            active_action_ = "Cut selection to clipboard";
        } else {
            // Priority 2: Fallback to original behavior (cut entire line)
            std::string current_line = editor_ptr->GetCurrentLineText();
            if (!current_line.empty()) {
                WriteSystemClipboard(current_line);
                editor_ptr->DeleteCurrentLine();
                active_action_ = "Cut line to clipboard";
            } else {
                active_action_ = "Nothing to cut";
            }
        }
        CloseDropdown();
        return;
    }
    
    if (item == 3) { // Copy
        if (editor_ptr->HasSelection()) {
            // Priority 1: Copy only the active Shift-selection
            std::string selected_text = editor_ptr->GetSelectedText();
            WriteSystemClipboard(selected_text);
            active_action_ = "Copied selection to clipboard";
        } else {
            // Priority 2: Fallback to original behavior (copy entire line)
            std::string current_line = editor_ptr->GetCurrentLineText();
            if (!current_line.empty()) {
                WriteSystemClipboard(current_line);
                active_action_ = "Copied line to clipboard";
            } else {
                active_action_ = "Nothing to copy";
            }
        }
        CloseDropdown();
        return;
    }
    
    if (item == 4) { // Paste stream
        CloseDropdown();
        FocusEditor();

        std::string clipboard_text = ReadSystemClipboard();
        if (!clipboard_text.empty()) {
            // NEW: If user pastes text while having an active selection, 
            // erase the highlighted block first (standard editor behavior)
            if (editor_ptr->HasSelection()) {
                editor_ptr->DeleteSelection();
            }

            active_action_ = "Pasted text (" + std::to_string(clipboard_text.size()) + " chars)";
            for (char c : clipboard_text) {
                if (c == '\r') continue;
                if (c == '\n') {
                    editor_ptr->OnEvent(ftxui::Event::Return);
                } else {
                    editor_ptr->OnEvent(ftxui::Event::Character(std::string(1, c)));
                }
            }
        } else {
            active_action_ = "Clipboard empty.";
        }
        return;
    }
    CloseDropdown();
}

void TextltApp::HandleOptionsMenu(int item) {
    if (item == 0) {
        editor_config_.show_line_numbers = !editor_config_.show_line_numbers;
        active_action_ = editor_config_.show_line_numbers ? "Line numbers enabled" : "Line numbers disabled";
        SaveConfig();
    } else if (item == 1) {
        editor_config_.show_file_explorer = !editor_config_.show_file_explorer;
        active_action_ = editor_config_.show_file_explorer ? "File Explorer enabled" : "File Explorer disabled";
        SaveConfig();
    } else if (item == 2) {
        OpenThemeDialog();
        return;
    }
    CloseDropdown();
}
    
    std::string TextltApp::ReadSystemClipboard() {
    std::string clipboard_text;
    char buffer[256];
    
    // Attempt 1: Standard X11 Clipboard via xclip
    FILE* pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (pipe) {
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            clipboard_text += buffer;
        }
        pclose(pipe);
    }

    // Attempt 2: Fallback to xsel
    if (clipboard_text.empty()) {
        pipe = popen("xsel --clipboard --output 2>/dev/null", "r");
        if (pipe) {
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                clipboard_text += buffer;
            }
            pclose(pipe);
        }
    }

    // Attempt 3: Fallback to X11 Primary selection (mouse highlight)
    if (clipboard_text.empty()) {
        pipe = popen("xclip -selection primary -o 2>/dev/null", "r");
        if (pipe) {
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                clipboard_text += buffer;
            }
            pclose(pipe);
        }
    }

    return clipboard_text;
}

void TextltApp::WriteSystemClipboard(const std::string& text) {
    if (text.empty()) return;
    FILE* pipe = popen("xclip -selection clipboard -i 2>/dev/null", "w");
    if (pipe) {
        std::fputs(text.c_str(), pipe);
        std::fflush(pipe);
        pclose(pipe);
    }
}

} // namespace textlt
