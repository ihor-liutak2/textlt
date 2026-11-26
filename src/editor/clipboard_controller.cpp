#include "editor/clipboard_controller.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "editor_component.hpp"

namespace textlt {
namespace {

constexpr std::string_view kPasteStart = "\x1B[200~";
constexpr std::string_view kPasteEnd = "\x1B[201~";
constexpr std::string_view kOsc52 = "\x1B]52;";

bool StartsWith(std::string_view input, std::string_view prefix) {
    return input.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view input, std::string_view suffix) {
    return input.size() >= suffix.size() && input.substr(input.size() - suffix.size()) == suffix;
}

bool WriteTerminal(std::string_view bytes) {
    return std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size() &&
        std::fflush(stdout) == 0;
}

#ifdef _WIN32

bool OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (OpenClipboard(owner)) {
            return true;
        }
        if (attempt < 3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }
    return false;
}

std::optional<std::string> ReadNativeClipboard() {
    if (!OpenClipboardWithRetry(nullptr)) {
        return std::nullopt;
    }
    std::optional<std::string> text;
    const HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        if (const auto* begin = static_cast<const wchar_t*>(GlobalLock(data))) {
            const auto end = begin + GlobalSize(data) / sizeof(wchar_t);
            const auto terminator = std::find(begin, end, L'\0');
            const auto length = terminator - begin;
            if (length < std::numeric_limits<int>::max() && terminator != end) {
                const int bytes = WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, begin, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
                if (bytes > 0) {
                    text.emplace(bytes, '\0');
                    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, begin,
                            static_cast<int>(length), text->data(), bytes, nullptr, nullptr)) {
                        text.reset();
                    }
                } else if (length == 0) {
                    text.emplace();
                }
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

bool WriteNativeClipboard(const std::string& text) {
    std::string windows_text;
    for (size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\r' || ch == '\n') {
            windows_text += "\r\n";
            if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
        } else {
            windows_text += ch;
        }
    }
    if (windows_text.size() >= std::numeric_limits<int>::max()) {
        return false;
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        windows_text.c_str(), -1, nullptr, 0);
    if (length == 0) {
        return false;
    }
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(length) * sizeof(wchar_t));
    if (!data) {
        return false;
    }
    auto* buffer = static_cast<wchar_t*>(GlobalLock(data));
    if (!buffer) {
        GlobalFree(data);
        return false;
    }
    const bool converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        windows_text.c_str(), -1, buffer, length) == length;
    GlobalUnlock(data);

    bool written = false;
    const HWND owner = converted ? CreateWindowExW(0, L"STATIC", L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr) : nullptr;
    if (owner) {
        if (OpenClipboardWithRetry(owner)) {
            written = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data);
            CloseClipboard();
        }
        DestroyWindow(owner);
    }
    if (!written) {
        GlobalFree(data);
    }
    return written;
}

#else

bool HasEnvironment(const char* variable) {
    const char* value = std::getenv(variable);
    return value && *value;
}

bool CommandAvailable(const std::string& command) {
    return std::system(("command -v " + command + " >/dev/null 2>&1").c_str()) == 0;
}

bool IsWslEnvironment() {
    if (HasEnvironment("WSL_INTEROP") || HasEnvironment("WSL_DISTRO_NAME")) {
        return true;
    }
    std::error_code error;
    return std::filesystem::exists("/proc/sys/fs/binfmt_misc/WSLInterop", error) ||
        CommandAvailable("powershell.exe");
}

bool CanUseOsc52() {
    const char* term = std::getenv("TERM");
    const char* terminal_clipboard = std::getenv("TEXTLT_TERMINAL_CLIPBOARD");
    const bool requested = terminal_clipboard && std::string_view(terminal_clipboard) == "1";
    return (requested || HasEnvironment("SSH_CONNECTION") ||
            HasEnvironment("SSH_TTY") || HasEnvironment("SSH_CLIENT")) &&
        isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && term && *term &&
        std::string_view(term) != "dumb";
}

bool WriteTextToPipe(const std::string& command, const std::string& text) {
    // A helper can exit before reading stdin, for example if its display is
    // unavailable. Keep the pipe open and drain it before reporting its status.
    const std::string script =
        "{ " + command + "; status=$?; cat; exit \"$status\"; } >/dev/null 2>&1";
    FILE* pipe = popen(script.c_str(), "w");
    if (!pipe) {
        return false;
    }

    const size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
    const int flush_status = std::fflush(pipe);
    const int close_status = pclose(pipe);
    return written == text.size() && flush_status == 0 && close_status == 0;
}

std::optional<std::string> ReadTextFromPipe(const std::string& command) {
    std::string clipboard_text;
    char buffer[256];
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return std::nullopt;
    }

    while (const size_t count = std::fread(buffer, 1, sizeof(buffer), pipe)) {
        clipboard_text.append(buffer, count);
    }
    const bool read_ok = !std::ferror(pipe);
    if (pclose(pipe) != 0 || !read_ok) {
        return std::nullopt;
    }
    return clipboard_text;
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

std::optional<std::string> Base64Decode(std::string_view input) {
    while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
        input.remove_suffix(1);
    }
    const size_t padding = input.find('=');
    if (padding != std::string_view::npos) {
        const auto suffix = input.substr(padding);
        if (input.size() % 4 != 0 || (suffix != "=" && suffix != "==")) {
            return std::nullopt;
        }
        input = input.substr(0, padding);
    }
    if (input.size() % 4 == 1) {
        return std::nullopt;
    }

    std::string output;
    output.reserve(input.size() * 3 / 4);
    unsigned int accumulator = 0;
    int bits = 0;
    for (char character : input) {
        const int value = Base64Value(character);
        if (value < 0) {
            return std::nullopt;
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

bool WriteWslClipboard(const std::string& text) {
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

std::optional<std::string> ReadWslClipboard() {
    if (!CommandAvailable("powershell.exe")) {
        return std::nullopt;
    }
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


struct ClipboardBackend {
    const char* tool;
    const char* command;
    bool wayland;
};

constexpr ClipboardBackend kReadBackends[] = {
    {"wl-paste", "wl-paste --no-newline --type text 2>/dev/null", true},
    {"xclip", "xclip -selection clipboard -o 2>/dev/null", false},
    {"xsel", "xsel --clipboard --output 2>/dev/null", false},
};

constexpr ClipboardBackend kWriteBackends[] = {
    {"wl-copy", "wl-copy --type 'text/plain;charset=utf-8'", true},
    {"xclip", "xclip -selection clipboard -i", false},
    {"xsel", "xsel --clipboard --input", false},
};

bool BackendAvailable(const ClipboardBackend& backend) {
    const char* session = std::getenv("XDG_SESSION_TYPE");
    const bool has_display = backend.wayland
        ? HasEnvironment("WAYLAND_DISPLAY") || HasEnvironment("WAYLAND_SOCKET") ||
            (session && std::string_view(session) == "wayland")
        : HasEnvironment("DISPLAY");
    return has_display && CommandAvailable(backend.tool);
}

std::optional<std::string> ReadNativeClipboard() {
    if (IsWslEnvironment()) {
        if (auto text = ReadWslClipboard()) {
            return text;
        }
    }
    for (const auto& backend : kReadBackends) {
        if (BackendAvailable(backend)) {
            if (auto text = ReadTextFromPipe(backend.command)) {
                return text;
            }
        }
    }
    return std::nullopt;
}

bool WriteNativeClipboard(const std::string& text) {
    if (IsWslEnvironment() && WriteWslClipboard(text)) {
        return true;
    }
    for (const auto& backend : kWriteBackends) {
        if (BackendAvailable(backend) && WriteTextToPipe(backend.command, text)) {
            return true;
        }
    }
    return false;
}

#endif

} // namespace

ClipboardController::ClipboardController(std::function<void()> request_redraw)
    : request_redraw_(std::move(request_redraw)) {}

ClipboardController::~ClipboardController() {
    StopReadTimer();
}

void ClipboardController::StopReadTimer() {
    if (read_timer_.joinable()) {
        {
            const std::lock_guard lock(read_timer_mutex_);
            read_timer_stopped_ = true;
        }
        read_timer_condition_.notify_one();
        read_timer_.join();
    }
}

void ClipboardController::ReadText(PasteCallback deliver) {
#ifndef _WIN32
    if (CanUseOsc52()) {
        const auto now = std::chrono::steady_clock::now();
        if (now < terminal_clipboard_deadline_) {
            deliver(std::nullopt);
            return;
        }
        if (WriteTerminal("\x1B]52;c;?\x1B\\")) {
            terminal_clipboard_read_ = std::move(deliver);
            terminal_clipboard_deadline_ = now + std::chrono::seconds(1);
            if (request_redraw_) {
                StopReadTimer();
                read_timer_stopped_ = false;
                read_timer_ = std::thread([this, deadline = terminal_clipboard_deadline_] {
                    std::unique_lock lock(read_timer_mutex_);
                    if (!read_timer_condition_.wait_until(lock, deadline,
                            [this] { return read_timer_stopped_; })) {
                        lock.unlock();
                        request_redraw_();
                    }
                });
            }
            return;
        }
    }
#endif
    deliver(ReadNativeClipboard());
}

bool ClipboardController::WriteText(const std::string& text) const {
    if (text.empty() || text.find('\0') != std::string::npos) {
        return false;
    }
#ifndef _WIN32
    if (CanUseOsc52()) {
        // OSC 52 has no write acknowledgement; this only confirms transmission
        // to the terminal, not that the terminal accepted the clipboard content.
        if (WriteTerminal("\x1B]52;c;" + Base64Encode(text) + "\x1B\\")) {
            return true;
        }
    }
#endif
    return WriteNativeClipboard(text);
}

void ClipboardController::SetBracketedPasteMode(bool enabled) {
    WriteTerminal(enabled ? "\x1B[?2004h" : "\x1B[?2004l");
}

bool ClipboardController::HandleTerminalEvent(
    const ftxui::Event& event, const PasteCallback& deliver) {
    const std::string_view input = event.input();
#ifndef _WIN32
    if (terminal_clipboard_read_ && std::chrono::steady_clock::now() >= terminal_clipboard_deadline_) {
        auto callback = std::exchange(terminal_clipboard_read_, {});
        callback(ReadNativeClipboard());
    }
#endif
    if (event == ftxui::Event::Custom || event.is_cursor_position()) {
        return false;
    }
    if (input.size() >= kPasteStart.size() + kPasteEnd.size() &&
        StartsWith(input, kPasteStart) && EndsWith(input, kPasteEnd)) {
        terminal_clipboard_read_ = {};
        deliver(std::string(input.substr(kPasteStart.size(),
            input.size() - kPasteStart.size() - kPasteEnd.size())));
        return true;
    }
#ifndef _WIN32
    if (StartsWith(input, kOsc52)) {
        auto callback = std::exchange(terminal_clipboard_read_, {});
        terminal_clipboard_deadline_ = {};
        StopReadTimer();
        if (callback) {
            auto response = input.substr(kOsc52.size());
            const auto delimiter = response.find(';');
            if (delimiter != std::string_view::npos &&
                (response.substr(0, delimiter) == "c" || delimiter == 0)) {
                response.remove_prefix(delimiter + 1);
                if (EndsWith(response, "\x1B\\")) {
                    response.remove_suffix(2);
                    callback(Base64Decode(response));
                } else if (EndsWith(response, "\a")) {
                    response.remove_suffix(1);
                    callback(Base64Decode(response));
                } else {
                    callback(std::nullopt);
                }
            } else {
                callback(std::nullopt);
            }
        }
        return true;
    }
    // A focus or input change can invalidate the target of a pending read.
    terminal_clipboard_read_ = {};
#endif
    return false;
}

ClipboardController::ActionResult ClipboardController::CutFromEditor(EditorComponent& editor) const {
    if (editor.IsReadOnly()) {
        return {false, "Document is read-only"};
    }
    if (editor.HasSelection()) {
        const std::string selected_text = editor.GetSelectedText();
        if (!WriteText(selected_text)) {
            return {false, kWriteFailed};
        }
        editor.DeleteSelection();
        return {true, "Cut selection to clipboard"};
    }

    const std::string current_line = editor.GetCurrentLineText();
    if (current_line.empty()) {
        return {false, "Nothing to cut"};
    }

    if (!WriteText(current_line)) {
        return {false, kWriteFailed};
    }
    editor.DeleteCurrentLine();
    return {true, "Cut line to clipboard"};
}

ClipboardController::ActionResult ClipboardController::CopyFromEditor(EditorComponent& editor) const {
    if (editor.HasSelection()) {
        const std::string selected_text = editor.GetSelectedText();
        return {false, WriteText(selected_text)
            ? "Copied selection to clipboard" : kWriteFailed};
    }

    const std::string current_line = editor.GetCurrentLineText();
    if (current_line.empty()) {
        return {false, "Nothing to copy"};
    }

    return {false, WriteText(current_line)
        ? "Copied line to clipboard" : kWriteFailed};
}

ClipboardController::ActionResult ClipboardController::PasteIntoEditor(
    EditorComponent& editor, const std::string& text) {
    if (editor.IsReadOnly()) {
        return {false, "Document is read-only"};
    }
    if (text.empty()) {
        return {false, "Clipboard has no text."};
    }
    editor.InsertText(text);
    return {true, "Pasted text (" + std::to_string(text.size()) + " bytes)"};
}

} // namespace textlt
