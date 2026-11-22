#include "editor/clipboard_controller.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#include "editor_component.hpp"

namespace textlt {
namespace {

constexpr int kClipboardWriteAttempts = 4;
constexpr int kClipboardRetryDelayMs = 30;

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
    static const bool is_wsl = [] {
        if (std::getenv("WSL_INTEROP") || std::getenv("WSL_DISTRO_NAME")) {
            return true;
        }
        std::error_code error;
        if (std::filesystem::exists("/proc/sys/fs/binfmt_misc/WSLInterop", error)) {
            return true;
        }
        return CommandAvailable("clip.exe");
    }();
    return is_wsl;
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

// Windows can briefly own the clipboard while the user is copying elsewhere.
// A single clip.exe attempt then fails even though a retry would succeed.
bool WriteTextToClipExe(const std::string& text) {
#ifdef _WIN32
    const std::string command = "clip.exe 2>nul";
#else
    const std::string command = "clip.exe 2>/dev/null";
#endif
    for (int attempt = 0; attempt < kClipboardWriteAttempts; ++attempt) {
        if (WriteTextToPipe(command, text)) {
            return true;
        }
        if (attempt + 1 < kClipboardWriteAttempts) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kClipboardRetryDelayMs));
        }
    }
    return false;
}

std::string Base64Encode(const std::string& input) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    size_t index = 0;
    while (index + 2 < input.size()) {
        const unsigned int chunk =
            (static_cast<unsigned char>(input[index]) << 16) |
            (static_cast<unsigned char>(input[index + 1]) << 8) |
            static_cast<unsigned char>(input[index + 2]);
        output.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        output.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        output.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        output.push_back(kAlphabet[chunk & 0x3F]);
        index += 3;
    }

    const size_t remaining = input.size() - index;
    if (remaining == 1) {
        const unsigned int chunk = static_cast<unsigned char>(input[index]) << 16;
        output.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        output.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        output.push_back('=');
        output.push_back('=');
    } else if (remaining == 2) {
        const unsigned int chunk =
            (static_cast<unsigned char>(input[index]) << 16) |
            (static_cast<unsigned char>(input[index + 1]) << 8);
        output.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        output.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        output.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        output.push_back('=');
    }
    return output;
}

int Base64Value(char character) {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

std::string Base64Decode(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 3 / 4);

    unsigned int accumulator = 0;
    int bits = 0;
    for (char character : input) {
        if (character == '=') {
            break;
        }
        const int value = Base64Value(character);
        if (value < 0) {
            continue;
        }
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    return output;
}

// clip.exe writes bytes using the Windows console code page, so non-ASCII
// action text can be lost. The PowerShell path transports the payload as
// base64 and retries Set-Clipboard while the clipboard is locked.
bool WriteTextToWindowsClipboardWithPowerShell(const std::string& text) {
    if (!CommandAvailable("powershell.exe")) {
        return false;
    }

    const std::string command =
        "powershell.exe -NoProfile -NonInteractive -Command "
        "'$b64=[System.Console]::In.ReadToEnd();"
        "$text=[System.Text.Encoding]::UTF8.GetString("
        "[System.Convert]::FromBase64String($b64));"
        "for($i=0;$i -lt 5;$i++){"
        "try{Set-Clipboard -Value $text;exit 0}"
        "catch{Start-Sleep -Milliseconds 50}};"
        "exit 1' 2>/dev/null";
    return WriteTextToPipe(command, Base64Encode(text));
}

std::string ReadTextFromWindowsClipboard() {
    const std::string encoded = ReadTextFromPipe(
        "powershell.exe -NoProfile -NonInteractive -Command "
        "'$text=Get-Clipboard -Raw;"
        "if($null -eq $text){exit 0};"
        "[System.Convert]::ToBase64String("
        "[System.Text.Encoding]::UTF8.GetBytes($text))' 2>/dev/null");
    return Base64Decode(encoded);
}

bool ContainsNonAscii(const std::string& text) {
    for (char character : text) {
        if (static_cast<unsigned char>(character) >= 0x80) {
            return true;
        }
    }
    return false;
}

bool WriteTextToX11Clipboard(const std::string& text) {
    if (WriteTextToPipe("xclip -selection clipboard -i 2>/dev/null", text)) {
        return true;
    }
    return WriteTextToPipe("xsel --clipboard --input 2>/dev/null", text);
}

} // namespace

std::string ClipboardController::ReadText(bool prefer_windows_clipboard) const {
#ifdef _WIN32
    (void)prefer_windows_clipboard;
    return ReadTextFromPipe("powershell -NoProfile -Command Get-Clipboard 2>nul");
#else
    if (prefer_windows_clipboard && IsWslEnvironment()) {
        std::string windows_text = ReadTextFromWindowsClipboard();
        if (!windows_text.empty()) {
            return windows_text;
        }
    }
    std::string clipboard_text = ReadTextFromPipe("xclip -selection clipboard -o 2>/dev/null");
    if (clipboard_text.empty()) {
        clipboard_text = ReadTextFromPipe("xsel --clipboard --output 2>/dev/null");
    }
    if (clipboard_text.empty()) {
        clipboard_text = ReadTextFromPipe("xclip -selection primary -o 2>/dev/null");
    }
    if (clipboard_text.empty() && !prefer_windows_clipboard && IsWslEnvironment()) {
        // WSLg's clipboard bridge is not always available. Fall back to the
        // Windows clipboard so text copied in Windows can be pasted here.
        clipboard_text = ReadTextFromWindowsClipboard();
    }
    return clipboard_text;
#endif
}

bool ClipboardController::WriteText(const std::string& text) const {
    if (text.empty()) {
        return false;
    }

#ifdef _WIN32
    return WriteTextToClipExe(text);
#else
    if (IsWslEnvironment()) {
        bool windows_written = false;
        if (ContainsNonAscii(text)) {
            // clip.exe interprets its stdin with the console code page, so use
            // the encoding-safe PowerShell path for non-ASCII action text.
            windows_written = WriteTextToWindowsClipboardWithPowerShell(text);
        }
        if (!windows_written) {
            windows_written = CommandAvailable("clip.exe") && WriteTextToClipExe(text);
        }
        if (!windows_written) {
            windows_written = WriteTextToWindowsClipboardWithPowerShell(text);
        }
        // Mirror the same text into the X11 clipboard so pasting inside WSL
        // works without depending on the WSLg clipboard bridge.
        const bool x11_written = WriteTextToX11Clipboard(text);
        return windows_written || x11_written;
    }

    return WriteTextToX11Clipboard(text);
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
