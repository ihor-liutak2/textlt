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
            HandleGitMenu(item_index);
            return;
        case 5:
            if (item_index == 0) {
                OpenAboutDialog();
            } else {
                OpenHelpDialog();
            }
            return;
        case 6: if (item_index == 0) RequestExit(); return;
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

    if (item == 12) { // Search in Files...
        CloseDropdown();
        OpenSearchFilesModal();
        active_action_ = "Search in Files";
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


void TextltApp::HandleGitMenu(int item) {
    if (item == 0) {
        OpenGitModal();
        return;
    }
    if (item == 1) {
        OpenGitSettingsModal();
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
