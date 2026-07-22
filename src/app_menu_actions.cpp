#include "app.hpp"

#include <memory>
#include <string>

#include "editor/document_session.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {


void TextltApp::CloseDropdown() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::Main);
    FocusEditor();
}


void TextltApp::OpenDropdown() {
    UpdateFileMenuLabels();
    if (menu_bar_) {
        menu_bar_->OpenDropdown(0);
    }
    SetActiveLayer(UiLayer::Main);
}


void TextltApp::ActivateTopMenu() {
    OpenDropdown();
}


void TextltApp::UpdateFileMenuLabels() {
    if (!menu_bar_) {
        return;
    }

    menu_bar_->SetFileFavoriteLabel(document_file_controller_.IsActiveSessionFavorite());
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


void TextltApp::RunDropdownAction(int menu_index, int item_index) {
    const std::string command_id = menu_bar_ ? menu_bar_->CommandIdAt(menu_index, item_index) : "";
    if (command_id.empty()) {
        active_action_ = "Unknown menu action";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    RunCommand(command_id);
}
    
void TextltApp::CommandFileNew() {
    document_file_controller_.NewSession();
    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
    active_action_ = "New file";
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandFileManageFiles() {
    OpenFilesModal(FilesModalMode::Manage);
}


void TextltApp::CommandFileOpen() {
    OpenFilesModal(FilesModalMode::Open);
}


void TextltApp::CommandFileSaveAs() {
    OpenFilesModal(FilesModalMode::SaveAs);
}


void TextltApp::CommandFileImport() {
    OpenFilesModal(FilesModalMode::Import);
}


void TextltApp::CommandFileRecent() {
    OpenRecentFilesModal();
}


void TextltApp::CommandFileClose() {
    CloseCurrentFile();
}


void TextltApp::CommandFileCloseAll() {
    CloseAllOpenedFiles();
}


void TextltApp::CommandFileSave() {
    if (workspace_mode_ == WorkspaceMode::Notes) {
        std::string error;
        auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
        if (!notes->Save(error) && !error.empty()) {
            active_action_ = "Notes save failed: " + error;
        }
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    SaveCurrentFile();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandFileSaveAll() {
    SaveAllOpenedFiles();
}


void TextltApp::CommandFileToggleFavorite() {
    ToggleActiveFavorite();
}


void TextltApp::CommandEditUndo() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    editor_ptr->Undo();
    active_action_ = "Undo";
    CloseDropdown();
}


void TextltApp::CommandEditRedo() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    editor_ptr->Redo();
    active_action_ = "Redo";
    CloseDropdown();
}


void TextltApp::CommandEditSelectAll() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    editor_ptr->SelectAll();
    active_action_ = "Selected all text";
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditCut() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    const auto result = clipboard_controller_.CutFromEditor(*editor_ptr);
    active_action_ = result.message;
    CloseDropdown();
}


void TextltApp::CommandEditCopy() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const auto result = clipboard_controller_.CopyFromEditor(*editor_ptr);
    active_action_ = result.message;
    CloseDropdown();
}


void TextltApp::CommandEditPaste() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    CloseDropdown();
    FocusEditor();
    const auto result = clipboard_controller_.PasteIntoEditor(*editor_ptr);
    active_action_ = result.message;
}


void TextltApp::CommandEditToggleComment() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    editor_ptr->ToggleComment();
    active_action_ = "Toggle Comment";
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditToggleCase() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    editor_ptr->ToggleCase();
    active_action_ = "Toggle Case";
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditConvertIndents4To2() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    editor_ptr->Convert4To2Spaces();
    active_action_ = "Converted leading indents from 4 to 2 spaces";
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditConvertIndents2To4() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    FocusEditor();
    editor_ptr->Convert2To4Spaces();
    active_action_ = "Converted leading indents from 2 to 4 spaces";
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditFind() {
    CloseDropdown();
    OpenFindPanel(false);
    active_action_ = "Find";
}


void TextltApp::CommandEditReplace() {
    CloseDropdown();
    OpenFindPanel(true);
    active_action_ = "Replace";
}


void TextltApp::CommandSearchFiles() {
    CloseDropdown();
    OpenSearchFilesModal();
    active_action_ = "Search in Files";
}


void TextltApp::CommandTextProcessors() {
    CloseDropdown();
    OpenTextProcessorsModal();
    active_action_ = "Text Processors";
}


void TextltApp::CommandCustomProcessorBuilder() {
    CloseDropdown();
    OpenCustomProcessorBuilderModal();
    active_action_ = "Custom Processor Builder";
}


void TextltApp::CommandViewToggleLineNumbers() {
    editor_config_.show_line_numbers = !editor_config_.show_line_numbers;
    active_action_ = editor_config_.show_line_numbers ? "Line numbers enabled" : "Line numbers disabled";
    SaveConfig();
    CloseDropdown();
}


void TextltApp::CommandSidebarToggleFileExplorer() {
    ToggleFileExplorer();
    CloseDropdown();
}


void TextltApp::CommandEditorToggleSmartWordWrap() {
    editor_config_.smart_word_wrap = !editor_config_.smart_word_wrap;
    active_action_ = editor_config_.smart_word_wrap ? "Smart Word Wrap enabled" : "Smart Word Wrap disabled";
    UpdateOptionsMenuLabels();
    SaveConfig();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditorToggleSyntaxHighlighting() {
    editor_config_.syntax_highlighting = !editor_config_.syntax_highlighting;
    active_action_ = editor_config_.syntax_highlighting ? "Syntax Highlighting enabled" : "Syntax Highlighting disabled";
    UpdateOptionsMenuLabels();
    SaveConfig();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditorToggleAutoPairing() {
    editor_config_.auto_pairing = !editor_config_.auto_pairing;
    active_action_ = editor_config_.auto_pairing ? "Auto Pairing enabled" : "Auto Pairing disabled";
    UpdateOptionsMenuLabels();
    SaveConfig();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditorToggleAutoIndent() {
    editor_config_.auto_indent = !editor_config_.auto_indent;
    active_action_ = editor_config_.auto_indent ? "Smart Auto-Indent enabled" : "Smart Auto-Indent disabled";
    UpdateOptionsMenuLabels();
    SaveConfig();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditorToggleTabSize() {
    editor_config_.tab_size = editor_config_.tab_size == 2 ? 4 : 2;
    active_action_ = "Tab size set to " + std::to_string(editor_config_.tab_size) + " spaces";
    UpdateOptionsMenuLabels();
    SaveConfig();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandEditorConvertTabsToSpaces() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->ConvertTabsToSpaces();
    active_action_ = "Converted tabs to spaces";
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandThemeOpen() {
    OpenThemeDialog();
}


void TextltApp::CommandViewLayoutOpen() {
    OpenViewLayoutModal();
}


void TextltApp::CommandTtsPlay() {
    SetTtsHeaderActiveButton(TtsHeaderButton::Play);
    tts_modal_.Play();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandTtsPause() {
    SetTtsHeaderActiveButton(TtsHeaderButton::Pause);
    tts_modal_.Pause();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandTtsStop() {
    SetTtsHeaderActiveButton(TtsHeaderButton::Stop);
    tts_modal_.Stop();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CommandTtsNext() {
    SetTtsHeaderActiveButton(TtsHeaderButton::Next);
    tts_modal_.Next();
    screen_.PostEvent(ftxui::Event::Custom);
}



} // namespace textlt
