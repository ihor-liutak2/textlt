#include "modals/modal_ai_quick_actions.hpp"

#include <cassert>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

int main() {
    using namespace textlt;

    int translate_count = 0;
    int edit_count = 0;
    int close_count = 0;
    AiQuickActionsModalContent content(
        nullptr,
        [&](AiActionType action, std::string& error) {
            error.clear();
            if (action == AiActionType::Translate) {
                ++translate_count;
            } else {
                ++edit_count;
            }
            return true;
        },
        [&] { ++close_count; });

    content.Open();
    assert(content.HandleEvent(ftxui::Event::Character("1")));
    assert(translate_count == 1);
    assert(edit_count == 0);
    assert(close_count == 1);

    content.Open();
    assert(content.HandleEvent(ftxui::Event::Character("2")));
    assert(translate_count == 1);
    assert(edit_count == 1);
    assert(close_count == 2);

    content.Open();
    assert(content.HandleEvent(ftxui::Event::Escape));
    assert(close_count == 3);

    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    const ftxui::Event mouse_event = ftxui::Event::Mouse("", mouse);
    assert(!content.HandleEvent(mouse_event));
    assert(translate_count == 1);
    assert(edit_count == 1);
    assert(close_count == 3);

    AiQuickActionsModalContent rejected(
        nullptr,
        [](AiActionType, std::string& error) {
            error = "No model selected.";
            return false;
        },
        [&] { ++close_count; });
    rejected.Open();
    assert(rejected.HandleEvent(ftxui::Event::Character("1")));
    assert(close_count == 3);

    return 0;
}
