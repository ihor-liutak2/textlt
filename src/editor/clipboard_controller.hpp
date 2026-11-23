#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "ftxui/component/component.hpp"

namespace textlt {

class EditorComponent;

class ClipboardController {
public:
    using PasteCallback = std::function<void(std::optional<std::string>)>;
    using ReadCallback = std::function<void(PasteCallback)>;
    using WriteCallback = std::function<bool(const std::string&)>;

    static constexpr char kWriteFailed[] = "Could not confirm clipboard write.";

    struct ActionResult {
        bool changed_editor = false;
        std::string message;
    };

    explicit ClipboardController(std::function<void()> request_redraw = {});
    ~ClipboardController();

    void ReadText(PasteCallback deliver);
    bool WriteText(const std::string& text) const;
    bool HandleTerminalEvent(const ftxui::Event& event, const PasteCallback& deliver);
    static void SetBracketedPasteMode(bool enabled);

    static ftxui::Event PasteEvent(std::string text);
    static bool IsPasteEvent(const ftxui::Event& event);
    static std::optional<std::string> PastedText(const ftxui::Event& event, bool multiline = true);
    static std::string NormalizeText(std::string_view text, bool multiline = true);
    static ftxui::Component WithPaste(ftxui::Component input, bool multiline);
    static ftxui::Component MakeInput(
        ftxui::StringRef value, ftxui::StringRef placeholder, ftxui::InputOption option);

    ActionResult CutFromEditor(EditorComponent& editor) const;
    ActionResult CopyFromEditor(EditorComponent& editor) const;
    static ActionResult PasteIntoEditor(EditorComponent& editor, const std::string& text);

private:
    void StopReadTimer();

    std::function<void()> request_redraw_;
    std::thread read_timer_;
    std::mutex read_timer_mutex_;
    std::condition_variable read_timer_condition_;
    bool read_timer_stopped_ = false;
    PasteCallback terminal_clipboard_read_;
    std::chrono::steady_clock::time_point terminal_clipboard_deadline_{};
};

} // namespace textlt
