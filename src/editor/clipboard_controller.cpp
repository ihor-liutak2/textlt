#include "editor/clipboard_controller.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "editor_component.hpp"

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

std::string ReadTextFromPipe(const std::string& command) {
    std::string clipboard_text;
    char buffer[256];
    FILE* pipe = OpenPipe(command, "r");
    if (!pipe) {
        return clipboard_text;
    }

    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        clipboard_text += buffer;
    }
    ClosePipe(pipe);
    return clipboard_text;
}

} // namespace

std::string ClipboardController::ReadText() const {
#ifdef _WIN32
    return ReadTextFromPipe("powershell -NoProfile -Command Get-Clipboard 2>nul");
#else
    std::string clipboard_text = ReadTextFromPipe("xclip -selection clipboard -o 2>/dev/null");
    if (clipboard_text.empty()) {
        clipboard_text = ReadTextFromPipe("xsel --clipboard --output 2>/dev/null");
    }
    if (clipboard_text.empty()) {
        clipboard_text = ReadTextFromPipe("xclip -selection primary -o 2>/dev/null");
    }
    return clipboard_text;
#endif
}

bool ClipboardController::WriteText(const std::string& text) const {
    if (text.empty()) {
        return false;
    }

#ifdef _WIN32
    return WriteTextToPipe("clip 2>nul", text);
#else
    if (IsWslEnvironment() &&
        CommandAvailable("clip.exe") &&
        WriteTextToPipe("clip.exe 2>/dev/null", text)) {
        return true;
    }

    return WriteTextToPipe("xclip -selection clipboard -i 2>/dev/null", text);
#endif
}

ClipboardController::ActionResult ClipboardController::CutFromEditor(EditorComponent& editor) const {
    if (editor.HasSelection()) {
        const std::string selected_text = editor.GetSelectedText();
        WriteText(selected_text);
        editor.DeleteSelection();
        return {true, "Cut selection to clipboard"};
    }

    const std::string current_line = editor.GetCurrentLineText();
    if (current_line.empty()) {
        return {false, "Nothing to cut"};
    }

    WriteText(current_line);
    editor.DeleteCurrentLine();
    return {true, "Cut line to clipboard"};
}

ClipboardController::ActionResult ClipboardController::CopyFromEditor(EditorComponent& editor) const {
    if (editor.HasSelection()) {
        const std::string selected_text = editor.GetSelectedText();
        WriteText(selected_text);
        return {false, "Copied selection to clipboard"};
    }

    const std::string current_line = editor.GetCurrentLineText();
    if (current_line.empty()) {
        return {false, "Nothing to copy"};
    }

    WriteText(current_line);
    return {false, "Copied line to clipboard"};
}

ClipboardController::ActionResult ClipboardController::PasteIntoEditor(EditorComponent& editor) const {
    const std::string clipboard_text = ReadText();
    if (clipboard_text.empty()) {
        return {false, "Clipboard empty."};
    }

    editor.InsertText(clipboard_text);
    return {true, "Pasted text (" + std::to_string(clipboard_text.size()) + " chars)"};
}

} // namespace textlt
