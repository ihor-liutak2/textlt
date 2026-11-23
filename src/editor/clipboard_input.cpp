#include "editor/clipboard_controller.hpp"

#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {
namespace {

constexpr std::string_view kPasteEvent = "TextLT:Paste";

} // namespace

ftxui::Event ClipboardController::PasteEvent(std::string text) {
    return ftxui::Event::Special(std::string(kPasteEvent) + '\0' + std::move(text));
}

std::string ClipboardController::NormalizeText(std::string_view text, bool multiline) {
    std::string normalized;
    normalized.reserve(text.size());
    if (!multiline) {
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
            text.remove_suffix(1);
        }
    }
    for (size_t index = 0; index < text.size(); ++index) {
        const auto ch = static_cast<unsigned char>(text[index]);
        if (ch == '\r' || ch == '\n') {
            normalized += multiline ? '\n' : ' ';
            if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
        } else if (ch == '\t') {
            normalized += multiline ? '\t' : ' ';
        } else if (ch >= ' ' && ch != 127) {
            normalized += static_cast<char>(ch);
        }
    }
    return normalized;
}

bool ClipboardController::IsPasteEvent(const ftxui::Event& event) {
    const std::string_view input = event.input();
    return input.size() > kPasteEvent.size() && input.substr(0, kPasteEvent.size()) == kPasteEvent &&
        input[kPasteEvent.size()] == '\0';
}

std::optional<std::string> ClipboardController::PastedText(
    const ftxui::Event& event, bool multiline) {
    if (!IsPasteEvent(event)) {
        return std::nullopt;
    }
    return NormalizeText(std::string_view(event.input()).substr(kPasteEvent.size() + 1), multiline);
}

ftxui::Component ClipboardController::WithPaste(ftxui::Component input, bool multiline) {
    return ftxui::CatchEvent(input, [input, multiline](ftxui::Event event) {
        if (!input->Focused()) {
            return false;
        }
        auto text = PastedText(event, multiline);
        if (!text) {
            return false;
        }
        if (!text->empty()) {
            input->OnEvent(ftxui::Event::Character(std::move(*text)));
        }
        return true;
    });
}

ftxui::Component ClipboardController::MakeInput(
    ftxui::StringRef value, ftxui::StringRef placeholder, ftxui::InputOption option) {
    const bool multiline = option.multiline();
    return WithPaste(ftxui::Input(std::move(value), std::move(placeholder), std::move(option)),
        multiline);
}

} // namespace textlt
