#include <stdexcept>
#include <string>
#include <vector>

#include "editor/clipboard_controller.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/terminal_input_parser.hpp"
#include "ui_selectable_input.hpp"

namespace {

std::vector<ftxui::Event> Parse(const std::string& input) {
    std::vector<ftxui::Event> events;
    ftxui::TerminalInputParser parser(
        [&](ftxui::Event event) { events.push_back(std::move(event)); });
    for (char byte : input) {
        parser.Add(byte);
    }
    return events;
}

void AssertSingleSpecialEvent(const std::string& input) {
    const std::vector<ftxui::Event> events = Parse(input);
    if (events.size() != 1) {
        throw std::runtime_error("Input must produce exactly one event");
    }
    if (events.front().is_character()) {
        throw std::runtime_error("Input must produce a special event");
    }
    if (events.front().input() != input) {
        throw std::runtime_error("Input event bytes were not preserved");
    }
}

void AssertPasteGoesToFocusedInput() {
    std::string host;
    int cursor = 0;
    ftxui::InputOption single_line;
    single_line.multiline = false;
    auto host_input = textlt::MakeSelectableInput(
        host, cursor, "host", single_line, std::make_shared<textlt::InputSelection>());

    std::string document;
    bool submitted = false;
    ftxui::InputOption multiline;
    multiline.on_enter = [&] { submitted = true; };
    auto multiline_input = textlt::ClipboardController::MakeInput(&document, "", multiline);

    auto fields = ftxui::Container::Vertical({host_input, multiline_input});
    host_input->TakeFocus();
    fields->OnEvent(textlt::ClipboardController::PasteEvent("Київ\r\nserver\r\n"));
    if (host != "Київ server" || !document.empty()) {
        throw std::runtime_error("Paste must go only to the focused single-line field");
    }

    multiline_input->TakeFocus();
    fields->OnEvent(textlt::ClipboardController::PasteEvent("\r\n"));
    if (document != "\n" || submitted) {
        throw std::runtime_error("Pasting a newline must insert text without submitting the field");
    }
}

} // namespace

int main() {
    AssertSingleSpecialEvent("\x1Bь");
    AssertSingleSpecialEvent("\x1BЬ");
    AssertSingleSpecialEvent("\x1Bц");
    AssertSingleSpecialEvent("\x1BЦ");
    AssertSingleSpecialEvent("\x1B[200~Привіт\r\nsecond\tline\x1B[201~");
    AssertSingleSpecialEvent("\x1B]52;c;dGV4dA==\a");
    AssertSingleSpecialEvent("\x1B]52;c;dGV4dA==\x1B\\");
    AssertPasteGoesToFocusedInput();
    return 0;
}
