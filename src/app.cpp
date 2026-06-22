#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include "app_resources.hpp"
#include "ftxui/component/component_options.hpp"
#include "theme.hpp"
#include "file_manager.hpp"
#include "document.hpp"

namespace textlt {
namespace {

bool CommandAvailable(const std::string& command) {
#ifdef _WIN32
    const std::string check_command = "where " + command + " >nul 2>nul";
#else
    const std::string check_command = "command -v " + command + " >/dev/null 2>&1";
#endif
    return std::system(check_command.c_str()) == 0;
}

bool IsWslEnvironment() {
#ifdef _WIN32
    return false;
#else
    std::error_code error;
    if (std::filesystem::exists("/proc/sys/fs/binfmt_misc/WSLInterop", error)) {
        return true;
    }
    return CommandAvailable("clip.exe");
#endif
}

bool IsPlainFolderName(const std::string& name) {
    return !name.empty() &&
           name != "." &&
           name != ".." &&
           name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos &&
           !std::filesystem::path(name).has_parent_path() &&
           !std::filesystem::path(name).is_absolute();
}

std::string WithTrailingSeparator(std::filesystem::path path) {
    std::string value = path.lexically_normal().string();
    if (value.empty()) {
        value = std::filesystem::current_path().string();
    }
    if (!value.empty() && value.back() != '/' && value.back() != '\\') {
        value += std::filesystem::path::preferred_separator;
    }
    return value;
}

FILE* OpenPipe(const std::string& command, const char* mode) {
#ifdef _WIN32
    return _popen(command.c_str(), mode);
#else
    return popen(command.c_str(), mode);
#endif
}

int ClosePipe(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

bool WriteTextToPipe(const std::string& command, const std::string& text) {
    FILE* pipe = OpenPipe(command, "w");
    if (!pipe) {
        return false;
    }

    const size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
    std::fflush(pipe);
    const int close_status = ClosePipe(pipe);
    return written == text.size() && close_status == 0;
}

std::vector<std::string> BuiltInHelpLines() {
    return {
        "textlt Help",
        "",
        "File",
        "  Ctrl+O        Open a file",
        "  Ctrl+S        Save the current file",
        "  Ctrl+Q        Exit the editor",
        "",
        "Navigation",
        "  Arrow Keys    Move the cursor",
        "  Home          Move to the start of the current line",
        "  End           Move to the end of the current line",
        "  Page Up       Move one page up",
        "  Page Down     Move one page down",
        "  Ctrl+Left     Jump to the previous word",
        "  Ctrl+Right    Jump to the next word",
        "  Ctrl+Up       Jump to the previous paragraph",
        "  Ctrl+Down     Jump to the next paragraph",
        "  Shift+Ctrl+Up Select to the previous paragraph",
        "  Shift+Ctrl+Down Select to the next paragraph",
        "",
        "Search & Replace",
        "  Ctrl+F        Open Find Panel",
        "  Ctrl+R        Open Replace Panel",
        "  F3            Find Next Match",
        "  Shift+Enter   Find Previous Match",
        "  Escape        Close Find/Replace Panel",
        "",
        "Editing",
        "  Ctrl + /      Toggle Comment",
        "  Ctrl + T      Toggle Case",
        "  Tab           Indent selected lines, or insert spaces",
        "  Shift+Tab     Remove one indent level",
        "  Alt+Backspace Delete the previous word",
        "  Ctrl+Delete   Delete the next word",
        "",
        "Selection & Clipboard",
        "  Shift+Arrows  Extend the active text selection",
        "  Mouse Drag    Select text inside the editor viewport",
        "  Ctrl+A        Select all text",
        "  Ctrl+C        Copy selection or current line",
        "  Ctrl+X        Cut selection or current line",
        "  Ctrl+V        Paste clipboard text",
        "  Ctrl+Shift+V  Paste when terminal reserves Ctrl+V",
        "",
        "AI",
        "  Alt+H         Open Text-to-Speech",
        "  Alt+J         Open AI Actions",
        "  Alt+S         Open Assistant Settings",
        "",
        "Menus",
        "  Tab           Switch focus from File Explorer to Editor",
        "  Ctrl+B        Toggle File Explorer",
        "  Ctrl+B, O     Open File Explorer on the Opened tab",
        "  Alt+B         Toggle File Explorer between Opened and Project",
        "  F1            Open this help window",
        "  F2            Open the File menu",
        "  F3            Open the Edit menu",
        "  F4            Open the Options menu",
        "  Escape        Close the active dialog or menu",
        "",
        "Options",
        "  Toggle Line Numbers changes the editor gutter immediately.",
        "  Toggle File Explorer shows or hides the sidebar.",
        "  Auto Pairing inserts matching brackets and quotes.",
        "  Smart Auto-Indent carries indentation onto new lines.",
        "  Theme opens the dynamic theme selector.",
        "",
        "Syntax Highlighting",
        "  Language        Supported Extensions / Filenames",
        "  C / C++         .c, .cpp, .hpp, .h",
        "  C#              .cs",
        "  Go              .go",
        "  Rust            .rs",
        "  Bash Scripting  .sh, .bash, .bashrc",
        "  JavaScript      .js, .mjs, .cjs",
        "  TypeScript      .ts, .mts",
        "  React JSX/TSX   .jsx, .tsx",
        "  GraphQL         .graphql, .gql",
        "  Laravel Blade   .blade.php",
        "  PHP             .php",
        "  Python          .py",
        "  Ruby            .rb, Gemfile",
        "  Java            .java",
        "  HTML            .html, .htm",
        "  XML             .xml, .xsd, .xsl, .xslt",
        "  CSS             .css",
        "  JSON            .json",
        "  INI             .ini, .conf",
        "  Environment     .env, .env.local, .env.*",
        "  YAML            .yaml, .yml",
        "  Docker          Dockerfile, docker-compose.yml",
        "  SQL             .sql",
    };
}

} // namespace

TextltApp::TextltApp()
    : config_manager_("config.json"),
      editor_config_(config_manager_.Load()),
      themes_(LoadThemesFromConfiguredLocations()),
      current_theme_(FindThemeByName(themes_, editor_config_.active_theme_name)),
      screen_(ftxui::ScreenInteractive::Fullscreen()),
      text_editor_(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_)),
      sidebar_panel_(ftxui::Make<SidebarPanel>(
          [this](const std::filesystem::path& path) { OpenSidebarFile(path); },
          [this](size_t index) { ActivateOpenDocument(index); },
          &current_theme_,
          &git_manager_,
          &editor_config_)),
      file_dialog_(&current_theme_, [this](
          FilePromptMode mode,
          const std::string& path,
          std::string& error) {
          return ConfirmFileDialog(mode, path, error);
      }),
      path_operation_dialog_(&current_theme_, [this](
          PathOperationMode mode,
          const std::string& from,
          const std::string& to,
          std::string& error) {
          return ConfirmPathOperation(mode, from, to, error);
      }),
      help_dialog_(&current_theme_),
      recent_files_modal_(
          &current_theme_,
          &recent_files_history_,
          [this](const std::filesystem::path& path, std::string& error) {
              const bool opened = OpenFile(path.string(), error);
              if (opened) {
                  FocusEditor();
                  screen_.PostEvent(ftxui::Event::Custom);
              }
              return opened;
          }),
      tts_modal_(
          &current_theme_,
          &cloud_tts_pipeline_,
          [this] { QueueTtsBookPreparationFromCursor(); }),
      ai_actions_modal_(&current_theme_),
      assistant_settings_modal_(
          &current_theme_,
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      theme_dialog_(
          &current_theme_,
          [this](const std::string& theme_name) { PreviewTheme(theme_name); },
          [this](const std::string& theme_name) { SelectTheme(theme_name); }),
      unsaved_changes_dialog_(
          &current_theme_,
          [this] { SaveAndExit(); },
          [this] { DiscardAndExit(); },
          [this] { CloseExitConfirmationDialog(); }) {
    recent_files_history_.Load();
    menu_bar_ = ftxui::Make<MenuBarComponent>(
        [this](int menu_index, int item_index) {
            this->RunDropdownAction(menu_index, item_index);
        },
        &current_theme_);
    RestoreOpenedDocuments();
    EnsureOneOpenDocument();
    UpdateFileMenuLabels();
    UpdateOptionsMenuLabels();

    auto body_content = ftxui::Container::Horizontal({
        sidebar_panel_,
        text_editor_,
    });
    body_container_ = ftxui::CatchEvent(body_content, [this](ftxui::Event event) {
        if (event == ftxui::Event::Tab &&
            focused_layer_ == 0 &&
            (!menu_bar_ || !menu_bar_->IsDropdownOpen()) &&
            !file_dialog_.IsOpen() &&
            !help_dialog_.IsOpen() &&
            !recent_files_modal_.IsOpen() &&
            !tts_modal_.IsOpen() &&
            !ai_actions_modal_.IsOpen() &&
            !assistant_settings_modal_.IsOpen() &&
            !theme_dialog_.IsOpen() &&
            !sidebar_has_focus_) {
            text_editor_->OnEvent(event);
            return true;
        }
        return false;
    });

    // FIXED: Added missing underscore to match class declaration
    main_container_ = ftxui::Container::Vertical({
        menu_bar_,
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

    auto search_filter_transform = [this](const ftxui::EntryState& state) {
        ftxui::Element item = ftxui::text(
            std::string(state.state ? "[X] " : "[ ] ") + state.label);
        if (state.active) {
            item = item | ftxui::bold;
        }
        if (state.focused) {
            item = item |
                ftxui::bgcolor(current_theme_.menu_foreground) |
                ftxui::color(current_theme_.menu_background);
        } else {
            item = item | ftxui::color(current_theme_.menu_foreground);
        }
        return item;
    };

    ftxui::CheckboxOption match_case_option = ftxui::CheckboxOption::Simple();
    match_case_option.transform = search_filter_transform;
    match_case_option.on_change = [this] {
        std::static_pointer_cast<EditorComponent>(text_editor_)->ToggleSearchMatchCase();
        SaveConfig();
        RefreshFindMatches();
        screen_.PostEvent(ftxui::Event::Custom);
    };
    search_match_case_checkbox_ = ftxui::Checkbox(
        "Match Case", &editor_config_.search_match_case, match_case_option);

    ftxui::CheckboxOption whole_word_option = ftxui::CheckboxOption::Simple();
    whole_word_option.transform = search_filter_transform;
    whole_word_option.on_change = [this] {
        std::static_pointer_cast<EditorComponent>(text_editor_)->ToggleSearchWholeWord();
        SaveConfig();
        RefreshFindMatches();
        screen_.PostEvent(ftxui::Event::Custom);
    };
    search_whole_word_checkbox_ = ftxui::Checkbox(
        "Whole Word", &editor_config_.search_whole_word, whole_word_option);

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
    find_panel_fields_container_ = ftxui::Container::Tab({
        find_panel_find_container_,
        find_panel_replace_container_,
    }, &search_panel_tab_index_);
    find_panel_filters_container_ = ftxui::Container::Horizontal({
        search_match_case_checkbox_,
        search_whole_word_checkbox_,
    });
    find_panel_container_ = ftxui::Container::Vertical({
        find_panel_fields_container_,
        find_panel_filters_container_,
    });

    root_container_ = ftxui::Container::Tab({
        main_container_,
        file_dialog_.View(),
        path_operation_dialog_.View(),
        help_dialog_.View(),
        theme_dialog_.View(),
        find_panel_container_,
        goto_line_input_component_,
        unsaved_changes_dialog_.View(),
        tts_modal_.View(),
        ai_actions_modal_.View(),
        assistant_settings_modal_.View(),
    }, &focused_layer_);

    renderer_ = ftxui::Renderer(root_container_, [this] { return Render(); });
    global_shortcuts_ = ftxui::CatchEvent(
        renderer_, [this](ftxui::Event event) { return HandleGlobalEvent(event); });

    FocusEditor();
}

TextltApp::TextltApp(const std::vector<std::string>& files_to_open)
    : TextltApp() {
    if (!files_to_open.empty()) {
        open_documents_.clear();
        active_document_index_ = 0;
        std::static_pointer_cast<EditorComponent>(text_editor_)
            ->SetDocument(std::make_shared<Document>());
    }
    InitializeWithFiles(files_to_open);
    EnsureOneOpenDocument();
    PersistOpenedDocuments();
}

void TextltApp::Run() {
    screen_.ForceHandleCtrlC(false);
    screen_.Loop(global_shortcuts_);
    PersistOpenedDocuments();
}

void TextltApp::CloseDropdown() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenDropdown() {
    UpdateFileMenuLabels();
    if (menu_bar_) {
        menu_bar_->OpenDropdown(0);
    }
    focused_layer_ = 0;
}

void TextltApp::CloseFileDialog() {
    file_dialog_.Close();
    exit_after_save_as_ = false;
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::ClosePathOperationDialog() {
    path_operation_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenFileDialog(FilePromptMode mode) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    focused_layer_ = 1;

    std::string default_path = WithTrailingSeparator(CurrentSidebarDirectory());
    if (mode == FilePromptMode::SaveAs) {
        const std::string current_path =
            std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
        if (!current_path.empty() &&
            current_path != "Untitled" &&
            current_path != "untitled.txt") {
            const std::string filename = std::filesystem::path(current_path).filename().string();
            if (!filename.empty()) {
                default_path += filename;
            }
        }
    } else if (mode == FilePromptMode::DeleteFile) {
        default_path = SelectedSidebarFileName();
    }

    file_dialog_.Open(mode, default_path);
}

void TextltApp::OpenPathOperationDialog(PathOperationMode mode) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    focused_layer_ = 2;
    path_operation_dialog_.Open(
        mode,
        SelectedSidebarPathName(),
        CurrentProjectPathCandidates());
}

std::filesystem::path TextltApp::CurrentSidebarDirectory() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->CurrentPath();
}

std::string TextltApp::SelectedSidebarFileName() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)
        ->GetSelectedFileNameInCurrentDirectory()
        .string();
}

std::string TextltApp::SelectedSidebarPathName() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)
        ->GetSelectedPathInCurrentDirectory()
        .string();
}

void TextltApp::OpenAboutDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    help_dialog_.OpenContent("About", {
        "textlt — Modern TUI Text Editor",
        "Version: 1.0.0 (Stable Release)",
        "",
        "Author: Ihor Liutak (ihorlt)",
        "Contact: ihorlt@gmail.com",
        "Origin: Ukraine 🇺🇦",
        "",
        "Built with: C++17 & FTXUI Framework",
        "License: MIT License (c) 2026",
    }, true);
    active_action_ = "Opened About";
    focused_layer_ = 3;
}

void TextltApp::OpenHelpDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    help_dialog_.OpenContent("Help", BuiltInHelpLines(), false);
    active_action_ = "Opened Help";
    focused_layer_ = 3;
}

void TextltApp::CloseHelpDialog() {
    help_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenRecentFilesModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    recent_files_modal_.Open();
    active_action_ = "Opened Recent Files";
    focused_layer_ = 8;
}

void TextltApp::CloseRecentFilesModal() {
    recent_files_modal_.Close();
    FocusEditor();
}

void TextltApp::OpenTtsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    tts_modal_.Open();
    active_action_ = "Opened Text-to-Speech";
    focused_layer_ = 9;
}

void TextltApp::CloseTtsModal() {
    tts_modal_.Close();
    FocusEditor();
}

void TextltApp::OpenAiActionsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    ai_actions_modal_.Open();
    active_action_ = "Opened AI Actions";
    focused_layer_ = 10;
}

void TextltApp::CloseAiActionsModal() {
    ai_actions_modal_.Close();
    FocusEditor();
}

void TextltApp::OpenAssistantSettingsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    EnsureAssistantResources();
    assistant_settings_modal_.Open();
    active_action_ = "Opened Assistant Settings";
    focused_layer_ = 11;
}

void TextltApp::CloseAssistantSettingsModal() {
    assistant_settings_modal_.Close();
    FocusEditor();
}

void TextltApp::OpenThemeDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    focused_layer_ = 4;
    theme_dialog_.Open(themes_, editor_config_.active_theme_name);
}

void TextltApp::CloseThemeDialog() {
    theme_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::ShowExitConfirmationDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    const std::string file_path = editor->CurrentFilePath();
    const std::string filename = std::filesystem::path(file_path).filename().string();
    const std::string display_name = filename.empty() ? file_path : filename;

    unsaved_changes_dialog_.Open(display_name);
    focused_layer_ = 7;
    active_action_ = "Unsaved changes";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseExitConfirmationDialog() {
    unsaved_changes_dialog_.Close();
    exit_after_save_as_ = false;
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::RequestExit() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const bool has_only_empty_memory_document =
        open_documents_.size() == 1 &&
        IsMemoryOnlyDocument(open_documents_.front()) &&
        editor_ptr->GetAllText().empty();
    if (has_only_empty_memory_document) {
        PersistActiveFavoriteCursor();
        screen_.Exit();
        return;
    }

    if (editor_ptr->IsDirty()) {
        ShowExitConfirmationDialog();
        return;
    }
    PersistActiveFavoriteCursor();
    screen_.Exit();
}

void TextltApp::SaveAndExit() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const std::string current_path = editor_ptr->CurrentFilePath();
    if (current_path.empty() || current_path == "Untitled" || current_path == "untitled.txt") {
        unsaved_changes_dialog_.Close();
        exit_after_save_as_ = true;
        OpenFileDialog(FilePromptMode::SaveAs);
        return;
    }

    std::string error;
    if (SaveFile(current_path, error)) {
        PersistActiveFavoriteCursor();
        screen_.Exit();
        return;
    }

    active_action_ = error.empty() ? "Save failed" : error;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::DiscardAndExit() {
    PersistActiveFavoriteCursor();
    screen_.Exit();
}

void TextltApp::ActivateTopMenu() {
    OpenDropdown();
}

void TextltApp::SwitchEditorFocus() {
    if (!editor_config_.show_file_explorer) {
        FocusEditor();
        return;
    }
    if (sidebar_has_focus_) {
        FocusEditor();
    } else {
        FocusSidebar();
    }
}

void TextltApp::FocusEditor() {
    sidebar_has_focus_ = false;
    focused_layer_ = 0;
    text_editor_->TakeFocus();
}

void TextltApp::FocusSidebar() {
    sidebar_has_focus_ = true;
    focused_layer_ = 0;
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->FocusMenu();
}

void TextltApp::ToggleFileExplorer() {
    editor_config_.show_file_explorer = !editor_config_.show_file_explorer;
    if (editor_config_.show_file_explorer) {
        active_action_ = "File Explorer enabled";
        FocusSidebar();
    } else {
        active_action_ = "File Explorer disabled";
        FocusEditor();
    }
    SaveConfig();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::HandleCtrlBFileExplorer() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        sidebar->ShowProject();
        active_action_ = "File Explorer enabled";
        FocusSidebar();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (!sidebar_has_focus_) {
        sidebar->ShowProject();
        active_action_ = "Project files";
        FocusSidebar();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    editor_config_.show_file_explorer = false;
    active_action_ = "File Explorer disabled";
    FocusEditor();
    SaveConfig();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShowOpenedFilesSidebar() {
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        SaveConfig();
    }

    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ShowOpenedFiles();
    FocusSidebar();
    active_action_ = "Opened files";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ToggleSidebarOpenedProject() {
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        SaveConfig();
    }

    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ToggleOpenedProject();
    FocusSidebar();
    active_action_ = "Switched File Explorer tab";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenFindPanel(bool replace_mode) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
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
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
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
    
    
    
bool TextltApp::SaveFile(const std::string& path, std::string& error) {
    try {
        const auto doc = ActiveDocument();
        if (!file_manager_.SaveAs(doc, path, error)) {
            throw std::runtime_error(error);
        }
        RefreshOpenedDocumentsSidebar();
        RefreshProjectSidebar();
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
        git_manager_.Invalidate();
        PersistOpenedDocuments();
        active_action_ = "Saved " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}

std::shared_ptr<Document> TextltApp::ActiveDocument() const {
    if (active_document_index_ < open_documents_.size()) {
        return open_documents_[active_document_index_];
    }
    return std::static_pointer_cast<EditorComponent>(text_editor_)->GetDocument();
}

int TextltApp::FindOpenDocument(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(path, error);
    if (error) {
        normalized = path;
    }

    for (size_t index = 0; index < open_documents_.size(); ++index) {
        const auto& doc = open_documents_[index];
        if (!doc) continue;

        std::filesystem::path doc_path = std::filesystem::absolute(doc->path, error);
        if (error) {
            doc_path = doc->path;
            error.clear();
        }
        if (doc_path == normalized) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void TextltApp::AddOpenDocument(std::shared_ptr<Document> doc) {
    if (!doc) {
        return;
    }

    open_documents_.push_back(doc);
    active_document_index_ = open_documents_.size() - 1;
    std::static_pointer_cast<EditorComponent>(text_editor_)->SetDocument(doc);
    RefreshOpenedDocumentsSidebar();
}

bool TextltApp::IsMemoryOnlyDocument(const std::shared_ptr<Document>& doc) const {
    if (!doc) {
        return false;
    }
    const std::string path = doc->path.string();
    return path.empty() || path == "Untitled" || path == "untitled.txt";
}

void TextltApp::EnsureOneOpenDocument() {
    if (!open_documents_.empty()) {
        if (active_document_index_ >= open_documents_.size()) {
            active_document_index_ = open_documents_.size() - 1;
        }
        std::static_pointer_cast<EditorComponent>(text_editor_)
            ->SetDocument(open_documents_[active_document_index_]);
        RefreshOpenedDocumentsSidebar();
        return;
    }

    auto doc = std::make_shared<Document>();
    doc->Reset();
    AddOpenDocument(doc);
}

void TextltApp::RemoveOpenDocument(size_t index) {
    if (index >= open_documents_.size()) {
        return;
    }

    open_documents_.erase(open_documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (open_documents_.empty()) {
        active_document_index_ = 0;
        EnsureOneOpenDocument();
        return;
    }

    if (active_document_index_ >= open_documents_.size()) {
        active_document_index_ = open_documents_.size() - 1;
    } else if (index < active_document_index_) {
        --active_document_index_;
    }

    std::static_pointer_cast<EditorComponent>(text_editor_)
        ->SetDocument(open_documents_[active_document_index_]);
    RefreshOpenedDocumentsSidebar();
}

void TextltApp::CloseCurrentFile() {
    if (open_documents_.empty()) {
        EnsureOneOpenDocument();
        return;
    }

    const std::string closed_name =
        open_documents_[active_document_index_]
            ? open_documents_[active_document_index_]->path.filename().string()
            : "";
    RemoveOpenDocument(active_document_index_);
    PersistOpenedDocuments();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    active_action_ = "Closed " + (closed_name.empty() ? "file" : closed_name);
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseAllOpenedFiles() {
    open_documents_.clear();
    active_document_index_ = 0;
    EnsureOneOpenDocument();
    PersistOpenedDocuments();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    active_action_ = "Closed all files";
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::PersistOpenedDocuments() {
    OpenedConfig opened_config;
    bool active_index_saved = false;

    for (size_t doc_index = 0; doc_index < open_documents_.size(); ++doc_index) {
        const auto& doc = open_documents_[doc_index];
        if (!doc) {
            continue;
        }

        OpenedFileState entry;
        entry.row = doc->cursor_row;
        entry.column = doc->cursor_col;

        if (IsMemoryOnlyDocument(doc)) {
            const std::string content = doc->ToContent();
            if (content.empty()) {
                continue;
            }
            entry.memory_only = true;
            entry.path = "Untitled";
            entry.content = content;
            if (doc_index == active_document_index_) {
                opened_config.active_index = opened_config.files.size();
                active_index_saved = true;
            }
            opened_config.files.push_back(std::move(entry));
            continue;
        }

        const std::string normalized = EditorConfig::NormalizeFavoritePath(doc->path.string());
        if (normalized.empty()) {
            continue;
        }
        std::error_code error_code;
        if (!std::filesystem::is_regular_file(normalized, error_code)) {
            continue;
        }
        entry.memory_only = false;
        entry.path = normalized;
        if (doc_index == active_document_index_) {
            opened_config.active_index = opened_config.files.size();
            active_index_saved = true;
        }
        opened_config.files.push_back(std::move(entry));
    }

    if (!active_index_saved || opened_config.active_index >= opened_config.files.size()) {
        opened_config.active_index = opened_config.files.empty() ? 0 : opened_config.files.size() - 1;
    }
    opened_config_store_.Save(opened_config);
}

void TextltApp::OpenRestoredDocument(const OpenedFileState& entry) {
    if (entry.memory_only) {
        auto doc = std::make_shared<Document>();
        doc->Reset();
        doc->LoadContent(entry.content, "Untitled");
        doc->is_dirty = true;
        doc->SetCursorPosition(entry.row, entry.column);
        AddOpenDocument(doc);
        return;
    }

    std::error_code error_code;
    if (!std::filesystem::is_regular_file(entry.path, error_code)) {
        return;
    }

    std::string error;
    auto doc = file_manager_.Open(entry.path, error);
    if (!doc) {
        return;
    }
    doc->SetCursorPosition(entry.row, entry.column);
    AddOpenDocument(doc);
}

void TextltApp::RestoreOpenedDocuments() {
    const OpenedConfig opened_config = opened_config_store_.Load();
    if (opened_config.files.empty()) {
        return;
    }

    open_documents_.clear();
    active_document_index_ = 0;
    for (const OpenedFileState& entry : opened_config.files) {
        OpenRestoredDocument(entry);
    }

    if (!open_documents_.empty()) {
        active_document_index_ = std::min(opened_config.active_index, open_documents_.size() - 1);
        std::static_pointer_cast<EditorComponent>(text_editor_)
            ->SetDocument(open_documents_[active_document_index_]);
        RefreshOpenedDocumentsSidebar();
    }

    PersistOpenedDocuments();
}

void TextltApp::ActivateOpenDocument(size_t index) {
    if (index >= open_documents_.size() || !open_documents_[index]) {
        return;
    }

    PersistActiveFavoriteCursor();
    active_document_index_ = index;
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->SetDocument(open_documents_[index]);
    RestoreFavoriteCursor(open_documents_[index]->path.string());
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    PersistOpenedDocuments();
    active_action_ = "Switched to " + editor_ptr->CurrentFilePath();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::RefreshOpenedDocumentsSidebar() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    std::vector<SidebarPanel::OpenedFileEntry> entries;
    entries.reserve(open_documents_.size());

    for (size_t index = 0; index < open_documents_.size(); ++index) {
        const auto& doc = open_documents_[index];
        if (!doc) continue;

        std::string label = doc->path.filename().string();
        if (label.empty()) {
            label = doc->path.string();
        }
        entries.push_back({doc->path, label, doc->is_dirty, index == active_document_index_});
    }

    sidebar->SetOpenedFiles(std::move(entries), active_document_index_);
}

bool TextltApp::OpenFile(const std::string& path, std::string& error) {
    try {
        PersistActiveFavoriteCursor();
        const int open_index = FindOpenDocument(path);
        if (open_index >= 0) {
            ActivateOpenDocument(static_cast<size_t>(open_index));
            PersistOpenedDocuments();
            recent_files_history_.AddFile(path);
            active_action_ = "Opened " + path;
            return true;
        }

        auto doc = file_manager_.Open(path, error);
        if (!doc) {
            throw std::runtime_error(error);
        }

        AddOpenDocument(doc);

        RestoreFavoriteCursor(path);
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
        PersistOpenedDocuments();
        recent_files_history_.AddFile(path);
        active_action_ = "Opened " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
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


void TextltApp::InitializeWithFiles(const std::vector<std::string>& files_to_open) {
    if (files_to_open.empty()) {
        return;
    }

    const std::filesystem::path active_path = files_to_open.front();
    std::string error;
    if (std::filesystem::exists(active_path)) {
        OpenFile(active_path.string(), error);
    } else {
        auto doc = std::make_shared<Document>();
        doc->Reset();
        doc->SetPath(active_path);
        AddOpenDocument(doc);
        active_action_ = "New file " + active_path.string();
    }

    for (size_t index = 1; index < files_to_open.size(); ++index) {
        const std::filesystem::path path = files_to_open[index];
        if (std::filesystem::exists(path)) {
            OpenFile(path.string(), error);
        } else {
            auto doc = std::make_shared<Document>();
            doc->Reset();
            doc->SetPath(path);
            AddOpenDocument(doc);
        }
    }

    FocusEditor();
}

void TextltApp::OpenSidebarFile(const std::filesystem::path& path) {
    std::string error;
    if (OpenFile(path.string(), error)) {
        FocusEditor();
        screen_.PostEvent(ftxui::Event::Custom);
    }
}

void TextltApp::SaveCurrentFile() {
    const std::string& current_path =
        std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
    if (current_path.empty() || current_path == "Untitled" || current_path == "untitled.txt") {
        OpenFileDialog(FilePromptMode::SaveAs);
        return;
    }

    std::string error;
    SaveFile(current_path, error);
}

void TextltApp::SaveAllOpenedFiles() {
    size_t saved_count = 0;
    size_t skipped_count = 0;
    std::string first_error;

    for (const auto& doc : open_documents_) {
        if (!doc || !doc->is_dirty) {
            continue;
        }

        const std::string path = doc->path.string();
        if (path.empty() || path == "Untitled" || path == "untitled.txt") {
            ++skipped_count;
            continue;
        }

        std::string error;
        if (file_manager_.SaveAs(doc, path, error)) {
            ++saved_count;
        } else if (first_error.empty()) {
            first_error = error;
        }
    }

    RefreshOpenedDocumentsSidebar();
    git_manager_.Invalidate();
    PersistOpenedDocuments();

    if (!first_error.empty()) {
        active_action_ = first_error;
    } else if (skipped_count > 0) {
        active_action_ = "Saved " + std::to_string(saved_count) +
            " file(s); " + std::to_string(skipped_count) + " unsaved draft(s) need Save As";
    } else {
        active_action_ = "Saved " + std::to_string(saved_count) + " file(s)";
    }

    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

bool TextltApp::ConfirmFileDialog(
    FilePromptMode mode,
    const std::string& path,
    std::string& error) {
    bool success = false;
    if (mode == FilePromptMode::Open) {
        std::filesystem::path file_path(path);
        if (!file_path.is_absolute()) {
            file_path = CurrentSidebarDirectory() / file_path;
        }
        success = OpenFile(file_path.string(), error);
    } else if (mode == FilePromptMode::SaveAs) {
        std::filesystem::path file_path(path);
        if (!file_path.is_absolute()) {
            file_path = CurrentSidebarDirectory() / file_path;
        }
        success = SaveFile(file_path.string(), error);
    } else if (mode == FilePromptMode::CreateFolder) {
        success = CreateFolderInCurrentDirectory(path, error);
    } else if (mode == FilePromptMode::DeleteFolder) {
        success = DeleteFolderInCurrentDirectory(path, error);
    } else if (mode == FilePromptMode::DeleteFile) {
        success = DeleteFileInCurrentDirectory(path, error);
    } else {
        error = "No file action selected.";
        return false;
    }

    if (success && mode == FilePromptMode::SaveAs && exit_after_save_as_) {
        exit_after_save_as_ = false;
        PersistActiveFavoriteCursor();
        screen_.Exit();
    }
    return success;
}

bool TextltApp::CreateFolderInCurrentDirectory(const std::string& name, std::string& error) {
    if (!IsPlainFolderName(name)) {
        error = "Enter only a folder name.";
        return false;
    }

    const std::filesystem::path folder_name(name);
    const std::filesystem::path target = CurrentSidebarDirectory() / folder_name;
    std::error_code status_error;
    if (std::filesystem::exists(target, status_error)) {
        error = "Folder already exists.";
        return false;
    }

    std::error_code create_error;
    if (!std::filesystem::create_directory(target, create_error) || create_error) {
        error = create_error ? create_error.message() : "Unable to create folder.";
        return false;
    }

    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ = "Created folder " + target.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}

bool TextltApp::DeleteFileInCurrentDirectory(const std::string& name, std::string& error) {
    if (!IsPlainFolderName(name)) {
        error = "Enter only a file name.";
        return false;
    }

    const std::filesystem::path file_name(name);
    const std::filesystem::path target = CurrentSidebarDirectory() / file_name;
    std::error_code status_error;
    if (!std::filesystem::is_regular_file(target, status_error)) {
        error = "File does not exist.";
        return false;
    }

    std::error_code remove_error;
    if (!std::filesystem::remove(target, remove_error) || remove_error) {
        error = remove_error ? remove_error.message() : "Unable to delete file.";
        return false;
    }

    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ = "Deleted file " + target.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}

bool TextltApp::DeleteFolderInCurrentDirectory(const std::string& name, std::string& error) {
    if (!IsPlainFolderName(name)) {
        error = "Enter only a folder name.";
        return false;
    }

    const std::filesystem::path folder_name(name);
    const std::filesystem::path target = CurrentSidebarDirectory() / folder_name;
    std::error_code status_error;
    if (!std::filesystem::is_directory(target, status_error)) {
        error = "Folder does not exist.";
        return false;
    }

    std::error_code remove_error;
    if (!std::filesystem::remove(target, remove_error) || remove_error) {
        error = remove_error ? remove_error.message() : "Unable to delete folder.";
        return false;
    }

    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ = "Deleted folder " + target.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}

std::vector<std::string> TextltApp::CurrentProjectPathCandidates() const {
    std::vector<std::string> candidates;
    const std::filesystem::path root = CurrentSidebarDirectory();
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return candidates;
    }

    std::filesystem::recursive_directory_iterator iter(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && iter != end; iter.increment(error)) {
        const std::filesystem::directory_entry& entry = *iter;
        std::error_code status_error;
        if (!entry.is_directory(status_error) && !entry.is_regular_file(status_error)) {
            continue;
        }

        const std::filesystem::path relative = entry.path().lexically_relative(root);
        if (!relative.empty()) {
            candidates.push_back(relative.generic_string());
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

bool TextltApp::ResolveProjectRelativePath(
    const std::string& path,
    std::filesystem::path* resolved,
    std::string& error) const {
    if (path.empty()) {
        error = "Enter a path.";
        return false;
    }

    std::filesystem::path relative(path);
    if (relative.is_absolute()) {
        error = "Use a path inside the current folder.";
        return false;
    }

    relative = relative.lexically_normal();
    if (relative.empty() || relative == ".") {
        error = "Enter a path.";
        return false;
    }

    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            error = "Use a path inside the current folder.";
            return false;
        }
    }

    if (resolved) {
        *resolved = (CurrentSidebarDirectory() / relative).lexically_normal();
    }
    return true;
}

void TextltApp::UpdateOpenDocumentPathsAfterMove(
    const std::filesystem::path& from,
    const std::filesystem::path& to) {
    const std::filesystem::path source = std::filesystem::absolute(from).lexically_normal();
    const std::filesystem::path destination = std::filesystem::absolute(to).lexically_normal();
    bool changed = false;

    for (const auto& doc : open_documents_) {
        if (!doc || doc->path.empty() || doc->path == "Untitled" || doc->path == "untitled.txt") {
            continue;
        }

        const std::filesystem::path document_path =
            std::filesystem::absolute(doc->path).lexically_normal();
        if (document_path == source) {
            doc->SetPath(destination);
            changed = true;
            continue;
        }

        const std::filesystem::path relative = document_path.lexically_relative(source);
        if (!relative.empty()) {
            auto iter = relative.begin();
            if (iter != relative.end() && *iter != "..") {
                doc->SetPath(destination / relative);
                changed = true;
            }
        }
    }

    if (changed) {
        RefreshOpenedDocumentsSidebar();
        PersistOpenedDocuments();
    }
}

bool TextltApp::ConfirmPathOperation(
    PathOperationMode mode,
    const std::string& from,
    const std::string& to,
    std::string& error) {
    std::filesystem::path source;
    std::filesystem::path destination;
    if (!ResolveProjectRelativePath(from, &source, error) ||
        !ResolveProjectRelativePath(to, &destination, error)) {
        return false;
    }

    if (source == destination) {
        error = "Source and destination are the same.";
        return false;
    }

    std::error_code status_error;
    if (!std::filesystem::exists(source, status_error)) {
        error = "Source does not exist.";
        return false;
    }
    if (std::filesystem::exists(destination, status_error)) {
        error = "Destination already exists.";
        return false;
    }

    const std::filesystem::path parent = destination.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, status_error)) {
        error = "Destination folder does not exist.";
        return false;
    }

    std::error_code rename_error;
    std::filesystem::rename(source, destination, rename_error);
    if (rename_error) {
        error = rename_error.message();
        return false;
    }

    UpdateOpenDocumentPathsAfterMove(source, destination);
    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ =
        (mode == PathOperationMode::Rename ? "Renamed " : "Moved ") +
        source.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}

void TextltApp::PreviewTheme(const std::string& theme_name) {
    current_theme_ = FindThemeByName(themes_, theme_name);
    editor_config_.SetActiveThemeName(current_theme_.name);
    active_action_ = "Previewing theme " + current_theme_.name;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SelectTheme(const std::string& theme_name) {
    current_theme_ = FindThemeByName(themes_, theme_name);
    editor_config_.SetActiveThemeName(current_theme_.name);
    active_action_ = "Theme changed to " + current_theme_.name;
    screen_.PostEvent(ftxui::Event::Custom);
    CloseThemeDialog();
}

void TextltApp::SaveConfig() {
    config_manager_.Save(editor_config_);
}

std::string TextltApp::ActiveDocumentFavoritePath() const {
    const auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    return EditorConfig::NormalizeFavoritePath(editor_ptr->CurrentFilePath());
}

void TextltApp::PersistActiveFavoriteCursor() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const std::string favorite_path =
        EditorConfig::NormalizeFavoritePath(editor_ptr->CurrentFilePath());
    if (favorite_path.empty() || !editor_config_.IsFavorite(favorite_path)) {
        return;
    }

    const size_t row = editor_ptr->GetCursorRow();
    const size_t column = editor_ptr->GetCursorCol();
    editor_config_.UpdateFavoriteCursor(favorite_path, row, column);
}

void TextltApp::RestoreFavoriteCursor(const std::string& path) {
    const FavoriteEntry* favorite = editor_config_.FindFavorite(path);
    if (!favorite) {
        return;
    }

    std::static_pointer_cast<EditorComponent>(text_editor_)
        ->SetCursorPosition(favorite->row, favorite->column);
}

void TextltApp::QueueTtsBookPreparationFromCursor() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

    // Submit document content and cursor row index for TTS processing
    cloud_tts_pipeline_.Submit(
        editor_ptr->GetAllText(),
        editor_ptr->CurrentFilePath(),
        editor_ptr->GetCursorRow());

    active_action_ = "Queued TTS book preparation";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ToggleActiveFavorite() {
    const std::string favorite_path = ActiveDocumentFavoritePath();
    if (favorite_path.empty()) {
        active_action_ = "Save the file before adding it to favorites";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (editor_config_.IsFavorite(favorite_path)) {
        editor_config_.RemoveFavorite(favorite_path);
        active_action_ = "Removed favorite " + favorite_path;
    } else {
        std::error_code error;
        if (!std::filesystem::is_regular_file(favorite_path, error)) {
            active_action_ = "Save the file before adding it to favorites";
            CloseDropdown();
            screen_.PostEvent(ftxui::Event::Custom);
            return;
        }
        editor_config_.AddFavorite(favorite_path);
        PersistActiveFavoriteCursor();
        active_action_ = "Added favorite " + favorite_path;
    }

    UpdateFileMenuLabels();
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::UpdateFileMenuLabels() {
    if (!menu_bar_) {
        return;
    }

    const std::string favorite_path = ActiveDocumentFavoritePath();
    menu_bar_->SetFileFavoriteLabel(editor_config_.IsFavorite(favorite_path));
}

void TextltApp::UpdateOptionsMenuLabels() {
    if (!menu_bar_) {
        return;
    }

    menu_bar_->SetOptionLabels(
        editor_config_.smart_word_wrap,
        editor_config_.syntax_highlighting,
        editor_config_.auto_pairing,
        editor_config_.auto_indent,
        editor_config_.tab_size);
}

void TextltApp::RefreshProjectSidebar() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ShowProject();
    sidebar->Refresh();
}

void TextltApp::RunDropdownAction(int menu_index, int item_index) {
    // Generate simple trace actions for debugging inside the app status-bar
    active_action_ = "DEBUG: Menu=" + std::to_string(menu_index) +
                    " Item=" + std::to_string(item_index);

    // Sub-route requests explicitly based on the active dropdown catalog index
    switch (menu_index) {
        case 0: HandleFileMenu(item_index);     return;
        case 1: HandleEditMenu(item_index);     return;
        case 2: HandleOptionsMenu(item_index);  return;
        case 3:
            HandleAiMenu(item_index);
            return;
        case 4:
            if (item_index == 0) {
                OpenAboutDialog();
            } else {
                OpenHelpDialog();
            }
            return;
        case 5: if (item_index == 0) RequestExit(); return;
        default: CloseDropdown();                            return;
    }
}
    
    void TextltApp::HandleFileMenu(int item) {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

        switch (item) {
            case 0: // New File
                PersistActiveFavoriteCursor();
                {
                    auto doc = std::make_shared<Document>();
                    doc->Reset();
                    AddOpenDocument(doc);
                }
                PersistOpenedDocuments();
                active_action_ = "New file";
                screen_.PostEvent(ftxui::Event::Custom);
                break;

            case 1: OpenFileDialog(FilePromptMode::CreateFolder); return;
            case 2: OpenFileDialog(FilePromptMode::DeleteFolder); return;
            case 3: OpenFileDialog(FilePromptMode::DeleteFile); return;
            case 4: OpenPathOperationDialog(PathOperationMode::Rename); return;
            case 5: OpenPathOperationDialog(PathOperationMode::Move); return;
            case 6: OpenFileDialog(FilePromptMode::Open); return;
            case 7: OpenRecentFilesModal(); return;
            case 8: CloseCurrentFile(); return;
            case 9: CloseAllOpenedFiles(); return;
            case 10: SaveCurrentFile(); return;
            case 11: SaveAllOpenedFiles(); return;
            case 12: OpenFileDialog(FilePromptMode::SaveAs); return;
            case 13: ToggleActiveFavorite(); return;
            case 14: RequestExit(); return;

            default:
                CloseDropdown();
                return;
        }
        CloseDropdown();
        FocusEditor();
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

    if (item == 2) { // Select All
        FocusEditor();
        editor_ptr->SelectAll();
        active_action_ = "Selected all text";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 3) { // Cut
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
    
    if (item == 4) { // Copy
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
    
    if (item == 5) { // Paste stream
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

    if (item == 6) { // Toggle Comment
        FocusEditor();
        editor_ptr->ToggleComment();
        active_action_ = "Toggle Comment";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 7) { // Toggle Case
        FocusEditor();
        editor_ptr->ToggleCase();
        active_action_ = "Toggle Case";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 8) { // Convert Indents: 4 -> 2 Spaces
        FocusEditor();
        editor_ptr->Convert4To2Spaces();
        active_action_ = "Converted leading indents from 4 to 2 spaces";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 9) { // Convert Indents: 2 -> 4 Spaces
        FocusEditor();
        editor_ptr->Convert2To4Spaces();
        active_action_ = "Converted leading indents from 2 to 4 spaces";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 10) { // Find...
        CloseDropdown();
        OpenFindPanel(false);
        active_action_ = "Find";
        return;
    }

    if (item == 11) { // Replace...
        CloseDropdown();
        OpenFindPanel(true);
        active_action_ = "Replace";
        return;
    }

    CloseDropdown();
}

void TextltApp::HandleAiMenu(int item) {
    switch (item) {
        case 0:
            OpenTtsModal();
            return;
        case 1:
            OpenAiActionsModal();
            return;
        case 2:
            OpenAssistantSettingsModal();
            return;
        default:
            CloseDropdown();
            return;
    }
}

void TextltApp::HandleOptionsMenu(int item) {
    if (item == 0) {
        editor_config_.show_line_numbers = !editor_config_.show_line_numbers;
        active_action_ = editor_config_.show_line_numbers ? "Line numbers enabled" : "Line numbers disabled";
        SaveConfig();
    } else if (item == 1) {
        ToggleFileExplorer();
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
        editor_config_.auto_pairing = !editor_config_.auto_pairing;
        active_action_ = editor_config_.auto_pairing ? "Auto Pairing enabled" : "Auto Pairing disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 5) {
        editor_config_.auto_indent = !editor_config_.auto_indent;
        active_action_ = editor_config_.auto_indent ? "Smart Auto-Indent enabled" : "Smart Auto-Indent disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 6) {
        editor_config_.tab_size = editor_config_.tab_size == 2 ? 4 : 2;
        active_action_ = "Tab size set to " + std::to_string(editor_config_.tab_size) + " spaces";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 7) {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->ConvertTabsToSpaces();
        active_action_ = "Converted tabs to spaces";
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 8) {
        OpenThemeDialog();
        return;
    }
    CloseDropdown();
}
    
    std::string TextltApp::ReadSystemClipboard() {
    std::string clipboard_text;
    char buffer[256];

#ifdef _WIN32
    FILE* pipe = OpenPipe("powershell -NoProfile -Command Get-Clipboard 2>nul", "r");
    if (pipe) {
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            clipboard_text += buffer;
        }
        ClosePipe(pipe);
    }
    return clipboard_text;
#else
    // Attempt 1: Standard X11 Clipboard via xclip
    FILE* pipe = OpenPipe("xclip -selection clipboard -o 2>/dev/null", "r");
    if (pipe) {
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            clipboard_text += buffer;
        }
        ClosePipe(pipe);
    }

    // Attempt 2: Fallback to xsel
    if (clipboard_text.empty()) {
        pipe = OpenPipe("xsel --clipboard --output 2>/dev/null", "r");
        if (pipe) {
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                clipboard_text += buffer;
            }
            ClosePipe(pipe);
        }
    }

    // Attempt 3: Fallback to X11 Primary selection (mouse highlight)
    if (clipboard_text.empty()) {
        pipe = OpenPipe("xclip -selection primary -o 2>/dev/null", "r");
        if (pipe) {
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                clipboard_text += buffer;
            }
            ClosePipe(pipe);
        }
    }

    return clipboard_text;
#endif
}

void TextltApp::WriteSystemClipboard(const std::string& text) {
    if (text.empty()) return;

#ifdef _WIN32
    WriteTextToPipe("clip 2>nul", text);
#else
    if (IsWslEnvironment() &&
        CommandAvailable("clip.exe") &&
        WriteTextToPipe("clip.exe 2>/dev/null", text)) {
        return;
    }

    WriteTextToPipe("xclip -selection clipboard -i 2>/dev/null", text);
#endif
}

} // namespace textlt
