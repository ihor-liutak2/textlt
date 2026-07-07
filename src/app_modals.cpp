#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "app_resources.hpp"
#include "document.hpp"
#include "ftxui/component/event.hpp"
#include "theme.hpp"

namespace textlt {
namespace {

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
        "  Alt+Left      Switch to the previous editor pane",
        "  Alt+Right     Switch to the next editor pane",
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
        "  View Layout opens the pane and column manager.",
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
        "  Lua             .lua",
        "  INI             .ini, .conf",
        "  Environment     .env, .env.local, .env.*",
        "  YAML            .yaml, .yml",
        "  Docker          Dockerfile, docker-compose.yml",
        "  SQL             .sql",
    };
}

} // namespace


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
    SetActiveLayer(UiLayer::Help);
}


void TextltApp::OpenHelpDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    help_dialog_.OpenContent("Help", BuiltInHelpLines(), false);
    active_action_ = "Opened Help";
    SetActiveLayer(UiLayer::Help);
}


void TextltApp::CloseHelpDialog() {
    help_dialog_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
}


void TextltApp::OpenKeyboardShortcutsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    keyboard_shortcuts_modal_.Open();
    active_action_ = "Opened Keyboard Shortcuts";
    SetActiveLayer(UiLayer::KeyboardShortcuts);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseKeyboardShortcutsModal() {
    keyboard_shortcuts_modal_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenCustomProcessorBuilderModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    custom_processor_builder_modal_.Open();
    active_action_ = "Opened Custom Processor Builder";
    SetActiveLayer(UiLayer::CustomProcessorBuilder);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseCustomProcessorBuilderModal() {
    custom_processor_builder_modal_.Close();
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenRecentFilesModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    recent_files_modal_.Open();
    active_action_ = "Opened Recent Files";
    SetActiveLayer(UiLayer::RecentFiles);
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
    SetActiveLayer(UiLayer::SearchFiles);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseSearchFilesModal() {
    search_files_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenFilesModal(FilesModalMode mode) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    std::filesystem::path start_path = CurrentSidebarDirectory();
    std::string suggested_file_name;
    if (mode == FilesModalMode::SaveAs) {
        const std::string current_path =
            std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
        if (!current_path.empty() &&
            current_path != "Untitled" &&
            current_path != "untitled.txt") {
            const std::filesystem::path current_file_path(current_path);
            suggested_file_name = current_file_path.filename().string();
            if (!current_file_path.parent_path().empty()) {
                start_path = current_file_path.parent_path();
            }
        }
    }

    files_modal_.Open(mode, start_path, suggested_file_name);
    active_action_ = "Opened " +
        std::string(mode == FilesModalMode::Open ? "Open" :
            mode == FilesModalMode::SaveAs ? "Save As" :
            mode == FilesModalMode::Import ? "Import" :
            "Files") +
        " modal";
    SetActiveLayer(UiLayer::Files);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseFilesModal() {
    files_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenTextProcessorsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    text_processors_modal_.Open();
    active_action_ = "Opened Text Processors";
    SetActiveLayer(UiLayer::TextProcessors);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseTextProcessorsModal() {
    text_processors_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenRemoteConnectionsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    remote_connections_modal_.Open();
    active_action_ = "Opened Remote Connections";
    SetActiveLayer(UiLayer::RemoteConnections);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseRemoteConnectionsModal() {
    remote_connections_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenRemoteFilesModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    remote_files_modal_.Open();
    active_action_ = "Opened Remote Files";
    SetActiveLayer(UiLayer::RemoteFiles);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseRemoteFilesModal() {
    remote_files_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


bool TextltApp::GetTextProcessorTargetText(
    bool whole_document,
    std::string& text,
    std::string& error) {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

    if (whole_document) {
        text = editor_ptr->GetAllText();
        return true;
    }

    if (!editor_ptr->HasSelection()) {
        error = "No selected text. Select text or enable Whole document.";
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    text = editor_ptr->GetSelectedText();
    return true;
}


bool TextltApp::ReplaceTextProcessorTargetText(
    bool whole_document,
    const std::string& text,
    std::string& error) {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

    if (whole_document) {
        editor_ptr->SelectAll();
    } else if (!editor_ptr->HasSelection()) {
        error = "No selected text. Select text or enable Whole document.";
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    if (text.empty()) {
        editor_ptr->DeleteSelection();
    } else {
        editor_ptr->InsertText(text);
    }

    active_action_ = whole_document
        ? "Applied text processor to whole document"
        : "Applied text processor to selected text";
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


bool TextltApp::InsertImportedText(
    const std::filesystem::path& path,
    const std::string& text,
    std::string& error) {
    if (text.empty()) {
        error = "Imported file does not contain plain text.";
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    // DEBUG: dump imported text to file
    {
        std::ofstream dbg("/tmp/textlt_inserted.txt", std::ios::trunc);
        dbg << "Length: " << text.size() << " bytes, " << text.size() << " chars\n";
        dbg << "---BEGIN---\n";
        dbg << text;
        dbg << "\n---END---\n";
        // Show each line
        std::istringstream ss(text);
        std::string line;
        int i = 0;
        while (std::getline(ss, line)) {
            dbg << "LINE " << i++ << ": [" << line << "]\n";
        }
    }

    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->InsertText(text, true);

    const std::string filename = path.filename().string();
    active_action_ = filename.empty()
        ? "Imported text"
        : "Imported text from " + filename;

    // The files modal closes after this callback returns. Do not switch the
    // active component tree from inside its mouse/key event; the queued custom
    // event from ConfirmFilesModalAction restores editor focus once the modal
    // is fully closed.
    return true;
}


void TextltApp::OpenGitModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    git_manager_.RefreshNow();
    git_modal_.Open();
    active_action_ = "Opened Git";
    SetActiveLayer(UiLayer::Git);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseGitModal() {
    git_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}




bool TextltApp::OpenGitCompareDocuments(
    const std::string& left_title,
    const std::string& left_content,
    const std::string& right_title,
    const std::string& right_content,
    std::string& error) {
    if (left_title.empty()) {
        error = "Git compare title is empty.";
        return false;
    }

    auto make_git_doc = [](const std::string& title, const std::string& content) {
        auto doc = std::make_shared<Document>();
        doc->LoadContent(content, title.empty() ? "Git compare" : title);
        doc->read_only = true;
        doc->temporary = true;
        doc->is_dirty = false;
        return doc;
    };

    auto left_doc = make_git_doc(left_title, left_content);
    const size_t left_index = document_workspace_.AddDocument(left_doc);

    size_t right_index = left_index;
    if (!right_title.empty()) {
        auto right_doc = make_git_doc(right_title, right_content);
        right_index = document_workspace_.AddDocument(right_doc);
    }

    if (right_title.empty()) {
        SetEditorLayoutMode(EditorLayoutMode::Single);
        AssignDocumentToEditorPane(0, left_index);
        SetActiveEditorPane(0);
    } else {
        SetEditorLayoutMode(EditorLayoutMode::TwoColumns);
        AssignDocumentToEditorPane(0, left_index);
        AssignDocumentToEditorPane(1, right_index);
        SetEditorPaneRole(0, "Git Left");
        SetEditorPaneRole(1, "Git Right");
        SetActiveEditorPane(0);
    }

    document_workspace_.ActiveDocumentIndex() = left_index;
    active_action_ = right_title.empty()
        ? "Opened Git diff"
        : "Opened Git side-by-side compare";
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


void TextltApp::OpenGitSettingsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }

    git_settings_modal_.Open();
    active_action_ = "Opened Git Settings";
    SetActiveLayer(UiLayer::GitSettings);
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseGitSettingsModal() {
    git_settings_modal_.Close();
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
    SetTtsHeaderActiveButton(TtsHeaderButton::Open);
    tts_modal_.Open();
    active_action_ = "Opened Text-to-Speech";
    SetActiveLayer(UiLayer::Tts);
}


void TextltApp::CloseTtsModal() {
    tts_modal_.Close();
    FocusEditor();
}


void TextltApp::OpenViewLayoutModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::ViewLayout);
    view_layout_modal_.Open();
    active_action_ = "Opened View Layout";
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseViewLayoutModal() {
    view_layout_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::OpenAiActionsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    ai_actions_modal_.Open();
    active_action_ = "Opened AI Actions";
    SetActiveLayer(UiLayer::AiActions);
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
    SetActiveLayer(UiLayer::AssistantSettings);
}


void TextltApp::CloseAssistantSettingsModal() {
    assistant_settings_modal_.Close();
    FocusEditor();
}


void TextltApp::OpenThemeDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::Theme);
    theme_dialog_.Open(themes_, editor_config_.active_theme_name);
}


void TextltApp::CloseThemeDialog() {
    theme_dialog_.Close();
    SetActiveLayer(UiLayer::Main);
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
    SetActiveLayer(UiLayer::UnsavedChanges);
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
        document_workspace_.OpenDocuments().size() == 1 &&
        DocumentWorkspace::IsMemoryOnlyDocument(document_workspace_.OpenDocuments().front()) &&
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
        OpenFilesModal(FilesModalMode::SaveAs);
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
    SetActiveLayer(UiLayer::GoToLine);
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
