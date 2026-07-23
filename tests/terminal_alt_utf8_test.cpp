#include <stdexcept>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/component/terminal_input_parser.hpp"

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
        throw std::runtime_error("Alt+UTF-8 must produce exactly one event");
    }
    if (events.front().is_character()) {
        throw std::runtime_error("Alt+UTF-8 must produce a special event");
    }
    if (events.front().input() != input) {
        throw std::runtime_error("Alt+UTF-8 event bytes were not preserved");
    }
}

} // namespace

int main() {
    AssertSingleSpecialEvent("\x1Bь");
    AssertSingleSpecialEvent("\x1BЬ");
    AssertSingleSpecialEvent("\x1Bц");
    AssertSingleSpecialEvent("\x1BЦ");
    return 0;
}
