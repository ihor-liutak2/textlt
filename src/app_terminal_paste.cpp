#include "app.hpp"

#include <memory>
#include <string>

namespace textlt {

    bool TextltApp::HandleTerminalBracketedPaste(ftxui::Event event) {
        static constexpr char kPasteStart[] = "\x1B[200~";
        static constexpr char kPasteEnd[] = "\x1B[201~";

        const std::string& input = event.input();
        if (input == kPasteStart) {
            terminal_bracketed_paste_active_ = true;
            terminal_bracketed_paste_buffer_.clear();
            return true;
        }

        if (!terminal_bracketed_paste_active_) {
            return false;
        }

        if (input == kPasteEnd) {
            terminal_bracketed_paste_active_ = false;
            if (!terminal_bracketed_paste_buffer_.empty()) {
                std::static_pointer_cast<EditorComponent>(text_editor_)
                    ->InsertText(terminal_bracketed_paste_buffer_);
                active_action_ = "Pasted text (" +
                    std::to_string(terminal_bracketed_paste_buffer_.size()) +
                    " chars)";
            }
            terminal_bracketed_paste_buffer_.clear();
            screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }

        terminal_bracketed_paste_buffer_ += input;
        return true;
    }

} // namespace textlt
