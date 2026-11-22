#include "editor/clipboard_controller.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
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
#ifdef _WIN32
    FILE* pipe = OpenPipe(command, "w");
#else
    // A helper can exit before reading stdin, for example if its display is
    // unavailable. Keep the pipe open and drain it before reporting its status.
    FILE* pipe = OpenPipe(
        "{ " + command + "; status=$?; cat; exit \"$status\"; } >/dev/null 2>&1", "w");
#endif
    if (!pipe) {
        return false;
    }

    const size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
    const int flush_status = std::fflush(pipe);
    const int close_status = ClosePipe(pipe);
    return written == text.size() && flush_status == 0 && close_status == 0;
}

std::optional<std::string> ReadTextFromPipe(const std::string& command) {
    std::string clipboard_text;
    char buffer[256];
    FILE* pipe = OpenPipe(command, "r");
    if (!pipe) {
        return std::nullopt;
    }

    while (const size_t count = std::fread(buffer, 1, sizeof(buffer), pipe)) {
        clipboard_text.append(buffer, count);
    }
    const bool read_ok = !std::ferror(pipe);
    if (ClosePipe(pipe) != 0 || !read_ok) {
        return std::nullopt;
    }
    return clipboard_text;
}

// Windows can briefly own the clipboard while the user is copying elsewhere.
// A single clip.exe attempt then fails even though a retry would succeed.
bool WriteTextToClipExe(const std::string& text) {
#ifdef _WIN32
    const std::string command = "clip.exe 2>nul";
#else
    const std::string command = "clip.exe";
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
        "try{Set-Clipboard -Value $text -ErrorAction Stop;exit 0}"
        "catch{Start-Sleep -Milliseconds 50}};"
        "exit 1'";
    return WriteTextToPipe(command, Base64Encode(text));
}

std::optional<std::string> ReadTextFromWindowsClipboard() {
    const auto encoded = ReadTextFromPipe(
        "powershell.exe -NoProfile -NonInteractive -Command "
        "'$ErrorActionPreference=\"Stop\";$text=Get-Clipboard -Raw;"
        "if($null -eq $text){exit 0};"
        "[System.Convert]::ToBase64String("
        "[System.Text.Encoding]::UTF8.GetBytes($text))' 2>/dev/null");
    if (!encoded) {
        return std::nullopt;
    }
    return Base64Decode(*encoded);
}

bool HasDisplay(const char* variable) {
    const char* value = std::getenv(variable);
    return value && *value;
}

bool WriteTextToDesktopClipboard(const std::string& text) {
    if (HasDisplay("WAYLAND_DISPLAY") &&
        WriteTextToPipe("wl-copy --type 'text/plain;charset=utf-8'", text)) {
        return true;
    }
    return HasDisplay("DISPLAY") &&
        (WriteTextToPipe("xclip -selection clipboard -i", text) ||
         WriteTextToPipe("xsel --clipboard --input", text));
}

std::string ReadTextFromDesktopClipboard() {
    if (HasDisplay("WAYLAND_DISPLAY")) {
        if (auto text = ReadTextFromPipe("wl-paste --no-newline --type text 2>/dev/null")) {
            return *text;
        }
    }
    if (HasDisplay("DISPLAY")) {
        if (auto text = ReadTextFromPipe("xclip -selection clipboard -o 2>/dev/null")) {
            return *text;
        }
        return ReadTextFromPipe("xsel --clipboard --output 2>/dev/null").value_or("");
    }
    return {};
}

} // namespace

std::string ClipboardController::ReadText() const {
#ifdef _WIN32
    return ReadTextFromPipe("powershell -NoProfile -Command Get-Clipboard 2>nul").value_or("");
#else
    if (IsWslEnvironment()) {
        if (auto text = ReadTextFromWindowsClipboard()) {
            return *text;
        }
    }
    return ReadTextFromDesktopClipboard();
#endif
}

bool ClipboardController::WriteText(const std::string& text) const {
    if (text.empty()) {
        return false;
    }

#ifdef _WIN32
    return WriteTextToClipExe(text);
#else
    if (IsWslEnvironment() &&
        (WriteTextToWindowsClipboardWithPowerShell(text) ||
         (CommandAvailable("clip.exe") && WriteTextToClipExe(text)))) {
        return true;
    }

    return WriteTextToDesktopClipboard(text);
#endif
}

ClipboardController::ActionResult ClipboardController::CutFromEditor(EditorComponent& editor) const {
    if (editor.HasSelection()) {
        const std::string selected_text = editor.GetSelectedText();
        if (!WriteText(selected_text)) {
            return {false, "Could not write to the system clipboard"};
        }
        editor.DeleteSelection();
        return {true, "Cut selection to clipboard"};
    }

    const std::string current_line = editor.GetCurrentLineText();
    if (current_line.empty()) {
        return {false, "Nothing to cut"};
    }

    if (!WriteText(current_line)) {
        return {false, "Could not write to the system clipboard"};
    }
    editor.DeleteCurrentLine();
    return {true, "Cut line to clipboard"};
}

ClipboardController::ActionResult ClipboardController::CopyFromEditor(EditorComponent& editor) const {
    if (editor.HasSelection()) {
        const std::string selected_text = editor.GetSelectedText();
        return {false, WriteText(selected_text)
            ? "Copied selection to clipboard" : "Could not write to the system clipboard"};
    }

    const std::string current_line = editor.GetCurrentLineText();
    if (current_line.empty()) {
        return {false, "Nothing to copy"};
    }

    return {false, WriteText(current_line)
        ? "Copied line to clipboard" : "Could not write to the system clipboard"};
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
