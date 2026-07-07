#include "app.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "document.hpp"
#include "ftxui/component/event.hpp"

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

} // namespace


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
    PersistActiveFavoriteCursor();
    const size_t document_index = document_workspace_.AddUntitledDocument();
    document_workspace_.AssignDocumentToActivePane(document_index);
    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
    PersistOpenedDocuments();
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
    if (editor_ptr->HasSelection()) {
        const std::string selected_text = editor_ptr->GetSelectedText();
        WriteSystemClipboard(selected_text);
        editor_ptr->DeleteSelection();
        active_action_ = "Cut selection to clipboard";
    } else {
        const std::string current_line = editor_ptr->GetCurrentLineText();
        if (!current_line.empty()) {
            WriteSystemClipboard(current_line);
            editor_ptr->DeleteCurrentLine();
            active_action_ = "Cut line to clipboard";
        } else {
            active_action_ = "Nothing to cut";
        }
    }
    CloseDropdown();
}


void TextltApp::CommandEditCopy() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    if (editor_ptr->HasSelection()) {
        const std::string selected_text = editor_ptr->GetSelectedText();
        WriteSystemClipboard(selected_text);
        active_action_ = "Copied selection to clipboard";
    } else {
        const std::string current_line = editor_ptr->GetCurrentLineText();
        if (!current_line.empty()) {
            WriteSystemClipboard(current_line);
            active_action_ = "Copied line to clipboard";
        } else {
            active_action_ = "Nothing to copy";
        }
    }
    CloseDropdown();
}


void TextltApp::CommandEditPaste() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    CloseDropdown();
    FocusEditor();

    const std::string clipboard_text = ReadSystemClipboard();
    if (!clipboard_text.empty()) {
        active_action_ = "Pasted text (" + std::to_string(clipboard_text.size()) + " chars)";
        editor_ptr->InsertText(clipboard_text);
    } else {
        active_action_ = "Clipboard empty.";
    }
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
