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
          {
              " Undo ",
              " Redo ",
              " Cut ",
              " Copy ",
              " Paste ",
              " Toggle Comment   Ctrl+/ ",
              " Find... ",
              " Replace... ",
          },
          {
              " Toggle Line Numbers ",
              " Toggle File Explorer ",
              " Smart Word Wrap [ ] ",
              " Syntax Highlighting [X] ",
              " Tab Size: 4 spaces ",
              " Convert Tabs to Spaces ",
              " Theme... ",
          },
          {" About textlt ", " Keyboard Shortcuts "},
          {" Exit "},
      }),
      current_dropdown_entries_(dropdown_entries_[0]) {
    editor_config_.active_theme_name = current_theme_.name;
    UpdateOptionsMenuLabels();

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

    auto body_content = ftxui::Container::Horizontal({
        file_explorer_,
        text_editor_,
    });
    body_container_ = ftxui::CatchEvent(body_content, [this](ftxui::Event event) {
        if (event == ftxui::Event::Tab &&
            focused_layer_ == 0 &&
            active_dropdown_ < 0 &&
            !file_dialog_.IsOpen() &&
            !help_dialog_.IsOpen() &&
            !theme_dialog_.IsOpen() &&
            !explorer_has_focus_) {
            text_editor_->OnEvent(event);
            return true;
        }
        return false;
    });

    // FIXED: Added missing underscore to match class declaration
    main_container_ = ftxui::Container::Vertical({
        top_menu_,
        body_container_,
    });

    ftxui::InputOption find_input_option;
    find_input_option.multiline = false;
    find_input_option.on_change = [this] { RefreshFindMatches(); };
    find_input_option.on_enter = [this] { FindNext(); };
    find_input_ = ftxui::Input(&find_query_, "find text", find_input_option);
    replace_find_input_ = ftxui::Input(&find_query_, "find text", find_input_option);

    ftxui::InputOption replace_input_option;
    replace_input_option.multiline = false;
    replace_input_option.on_enter = [this] { FindNext(); };
    replace_input_ = ftxui::Input(&replace_text_, "replacement", replace_input_option);

    ftxui::InputOption goto_line_input_option;
    goto_line_input_option.multiline = false;
    goto_line_input_option.on_enter = [this] { SubmitGoToLine(); };
    goto_line_input_component_ = ftxui::Input(
        &goto_line_input_, "line number", goto_line_input_option);

    find_next_button_ = ftxui::Button("Find Next", [this] { FindNext(); });
    find_previous_button_ = ftxui::Button("Find Prev", [this] { FindPrevious(); });
    replace_next_button_ = ftxui::Button("Replace Next", [this] { ReplaceNext(); });
    replace_all_button_ = ftxui::Button("Replace All", [this] { ReplaceAll(); });

    find_panel_find_container_ = ftxui::Container::Horizontal({
        find_input_,
        find_next_button_,
        find_previous_button_,
    });
    find_panel_replace_container_ = ftxui::Container::Horizontal({
        replace_find_input_,
        replace_input_,
        replace_next_button_,
        replace_all_button_,
    });
    find_panel_container_ = ftxui::Container::Tab({
        find_panel_find_container_,
        find_panel_replace_container_,
    }, &search_panel_tab_index_);

    root_container_ = ftxui::Container::Tab({
        main_container_,
        dropdown_menu_,
        file_dialog_.View(),
        help_dialog_.View(),
        theme_dialog_.View(),
        find_panel_container_,
        goto_line_input_component_,
    }, &focused_layer_);

    renderer_ = ftxui::Renderer(root_container_, [this] { return Render(); });
    global_shortcuts_ = ftxui::CatchEvent(
        renderer_, [this](ftxui::Event event) { return HandleGlobalEvent(event); });

    FocusEditor();
}

TextltApp::TextltApp(const std::vector<std::string>& files_to_open)
    : TextltApp() {
    InitializeWithFiles(files_to_open);
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

void TextltApp::OpenFindPanel(bool replace_mode) {
    active_dropdown_ = -1;
    current_search_mode_ = replace_mode ? SearchMode::Replace : SearchMode::Find;
    search_panel_tab_index_ = replace_mode ? 1 : 0;
    focused_layer_ = 5;
    RefreshFindMatches();
    if (replace_mode) {
        replace_find_input_->TakeFocus();
    } else {
        find_input_->TakeFocus();
    }
}

void TextltApp::CloseFindPanel() {
    current_search_mode_ = SearchMode::None;
    std::static_pointer_cast<EditorComponent>(text_editor_)->ClearSearchHighlights();
    FocusEditor();
}

void TextltApp::OpenGoToLinePanel() {
    active_dropdown_ = -1;
    current_search_mode_ = SearchMode::None;
    std::static_pointer_cast<EditorComponent>(text_editor_)->ClearSearchHighlights();
    show_goto_line_bar_ = true;
    goto_line_input_.clear();
    focused_layer_ = 6;
    goto_line_input_component_->TakeFocus();
    active_action_ = "Go to line";
}

void TextltApp::CloseGoToLinePanel() {
    show_goto_line_bar_ = false;
    FocusEditor();
}

void TextltApp::SubmitGoToLine() {
    try {
        size_t parsed_chars = 0;
        const int line_number = std::stoi(goto_line_input_, &parsed_chars);
        if (parsed_chars != goto_line_input_.size()) {
            active_action_ = "Invalid line number";
        } else {
            auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
            editor_ptr->JumpToLine(line_number);
            active_action_ = "Jumped to line " + std::to_string(line_number);
        }
    } catch (const std::exception&) {
        active_action_ = "Invalid line number";
    }

    show_goto_line_bar_ = false;
    FocusEditor();
}

void TextltApp::RefreshFindMatches() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    if (find_query_.empty()) {
        active_action_ = "Find query empty";
    } else {
        active_action_ = FindMatchStatus();
    }
}

void TextltApp::FindNext() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    editor_ptr->JumpToNextMatch();
    active_action_ = FindMatchStatus();
}

void TextltApp::FindPrevious() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    editor_ptr->JumpToPreviousMatch();
    active_action_ = FindMatchStatus();
}

void TextltApp::ReplaceNext() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->ExecuteReplaceNext(find_query_, replace_text_);
    active_action_ = "Replace next: " + FindMatchStatus();
}

void TextltApp::ReplaceAll() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    const size_t count_before = editor_ptr->SearchMatchCount();
    editor_ptr->ExecuteReplaceAll(find_query_, replace_text_);
    active_action_ = "Replaced " + std::to_string(count_before) + " matches";
}

std::string TextltApp::FindMatchStatus() const {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const size_t count = editor_ptr->SearchMatchCount();
    if (find_query_.empty()) {
        return "Find";
    }
    if (count == 0) {
        return "No matches";
    }
    return "Match " + std::to_string(editor_ptr->CurrentSearchMatchIndex()) +
        " of " + std::to_string(count);
}

std::string TextltApp::FileTypeLabel(const std::string& file_path) const {
    const std::string extension = std::filesystem::path(file_path).extension().string();
    if (extension == ".cpp" || extension == ".hpp" ||
        extension == ".cc" || extension == ".h") {
        return "C++ Source";
    }
    if (extension == ".json") {
        return "JSON Document";
    }
    if (extension == ".txt") {
        return "Text Document";
    }
    if (extension == ".md") {
        return "Markdown Document";
    }
    if (extension == ".html" || extension == ".htm") {
        return "HTML Document";
    }
    if (extension == ".css") {
        return "CSS Stylesheet";
    }
    if (extension == ".js") {
        return "JavaScript Source";
    }
    if (extension == ".jsx") {
        return "React JSX Source";
    }
    if (extension == ".ts") {
        return "TypeScript Source";
    }
    if (extension == ".tsx") {
        return "React TSX Source";
    }
    if (extension == ".php") {
        return "PHP Script";
    }
    if (extension == ".java") {
        return "Java Source";
    }
    if (extension == ".py") {
        return "Python Script";
    }
    return "Plain Text";
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

void TextltApp::InitializeWithFiles(const std::vector<std::string>& files_to_open) {
    if (files_to_open.empty()) {
        return;
    }

    const std::filesystem::path active_path = files_to_open.front();
    std::string error;
    if (std::filesystem::exists(active_path)) {
        OpenFile(active_path.string(), error);
    } else {
        std::static_pointer_cast<EditorComponent>(text_editor_)->NewFile(active_path.string());
        active_action_ = "New file " + active_path.string();
    }

    if (files_to_open.size() > 1) {
        active_action_ += " (" + std::to_string(files_to_open.size() - 1) +
            " additional path(s) ignored: tabs not implemented)";
    }

    FocusEditor();
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

void TextltApp::UpdateOptionsMenuLabels() {
    if (dropdown_entries_.size() <= 2 || dropdown_entries_[2].size() <= 5) {
        return;
    }

    dropdown_entries_[2][2] = editor_config_.smart_word_wrap
        ? " Smart Word Wrap [X] "
        : " Smart Word Wrap [ ] ";
    dropdown_entries_[2][3] = editor_config_.syntax_highlighting
        ? " Syntax Highlighting [X] "
        : " Syntax Highlighting [ ] ";
    dropdown_entries_[2][4] =
        " Tab Size: " + std::to_string(editor_config_.tab_size) + " spaces ";

    if (active_dropdown_ == 2) {
        current_dropdown_entries_ = dropdown_entries_[2];
    }
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

    if (item == 0) { // Undo
        FocusEditor();
        editor_ptr->Undo();
        active_action_ = "Undo";
        CloseDropdown();
        return;
    }

    if (item == 1) { // Redo
        FocusEditor();
        editor_ptr->Redo();
        active_action_ = "Redo";
        CloseDropdown();
        return;
    }

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
            active_action_ = "Pasted text (" + std::to_string(clipboard_text.size()) + " chars)";
            editor_ptr->InsertText(clipboard_text);
        } else {
            active_action_ = "Clipboard empty.";
        }
        return;
    }

    if (item == 5) { // Toggle Comment
        FocusEditor();
        editor_ptr->ToggleComment();
        active_action_ = "Toggle Comment";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 6) { // Find...
        CloseDropdown();
        OpenFindPanel(false);
        active_action_ = "Find";
        return;
    }

    if (item == 7) { // Replace...
        CloseDropdown();
        OpenFindPanel(true);
        active_action_ = "Replace";
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
        editor_config_.smart_word_wrap = !editor_config_.smart_word_wrap;
        active_action_ = editor_config_.smart_word_wrap ? "Smart Word Wrap enabled" : "Smart Word Wrap disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 3) {
        editor_config_.syntax_highlighting = !editor_config_.syntax_highlighting;
        active_action_ = editor_config_.syntax_highlighting ? "Syntax Highlighting enabled" : "Syntax Highlighting disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 4) {
        editor_config_.tab_size = editor_config_.tab_size == 2 ? 4 : 2;
        active_action_ = "Tab size set to " + std::to_string(editor_config_.tab_size) + " spaces";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 5) {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->ConvertTabsToSpaces();
        active_action_ = "Converted tabs to spaces";
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 6) {
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
