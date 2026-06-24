#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app_resources.hpp"
#include "document.hpp"
#include "ftxui/component/event.hpp"
#include "theme.hpp"

namespace textlt {
namespace {

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


void TextltApp::OpenSearchFilesModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    search_files_modal_.Open();
    active_action_ = "Opened Search in Files";
    focused_layer_ = 0;
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseSearchFilesModal() {
    search_files_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


std::vector<FileSearchRoot> TextltApp::CurrentSearchFileRoots() const {
    const std::filesystem::path root = CurrentSidebarDirectory();

    std::string label = root.filename().string();
    if (label.empty()) {
        label = root.string();
    }
    if (label.empty()) {
        label = "Current Folder";
    }

    return {
        FileSearchRoot{root, label}
    };
}


bool TextltApp::OpenSearchFileMatch(const FileSearchMatch& match, std::string& error) {
    if (!OpenFile(match.path.string(), error)) {
        return false;
    }

    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->JumpToLine(match.line_number);

    active_action_ =
    "Opened search result " +
    match.relative_path.generic_string() +
    ":" +
    std::to_string(match.line_number);

    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
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

} // namespace textlt
