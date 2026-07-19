#include "modals/modal_ai_quick_actions.hpp"

#include <cassert>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"

int main() {
    using namespace textlt;

    int translate_count = 0;
    int edit_count = 0;
    int stop_count = 0;
    int close_count = 0;
    AiQuickStatusSnapshot status;
    status.model_label = "Gemma test";
    status.status_label = "Ready — llama.cpp";
    status.ready = true;

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
        [&] { return status; },
        [&] {
            ++stop_count;
            status.stopping = true;
        },
        [&] { ++close_count; });

    content.Open();
    assert(content.HandleEvent(ftxui::Event::Character("1")));
    assert(translate_count == 1);
    assert(edit_count == 0);
    assert(close_count == 0);

    status.ready = false;
    status.busy = true;
    status.active_action = AiActionType::Translate;
    status.status_label = "Working — translating current paragraph";
    assert(content.HandleEvent(ftxui::Event::Character("3")));
    assert(stop_count == 1);
    assert(close_count == 0);

    assert(content.HandleEvent(ftxui::Event::Escape));
    assert(stop_count == 1);
    assert(close_count == 0);

    status.busy = false;
    status.stopping = false;
    status.ready = true;
    status.status_label = "Ready — llama.cpp";
    assert(content.HandleEvent(ftxui::Event::Character("2")));
    assert(translate_count == 1);
    assert(edit_count == 1);
    assert(close_count == 0);

    assert(content.HandleEvent(ftxui::Event::Escape));
    assert(close_count == 1);

    status.ready = false;
    status.checking = true;
    status.status_label = "Checking AI backend...";
    assert(content.HandleEvent(ftxui::Event::Character("1")));
    assert(translate_count == 1);

    status.checking = false;
    status.status_label = "Not ready — model is unavailable";
    assert(content.HandleEvent(ftxui::Event::Character("2")));
    assert(edit_count == 1);

    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    const ftxui::Event mouse_event = ftxui::Event::Mouse("", mouse);
    assert(!content.HandleEvent(mouse_event));
    assert(translate_count == 1);
    assert(edit_count == 1);
    assert(stop_count == 1);
    assert(close_count == 1);

    return 0;
}
