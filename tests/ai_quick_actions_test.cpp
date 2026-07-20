#include "modals/modal_ai_quick_actions.hpp"
#include "modals/modal_window.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

namespace {

class FooterTestContent final : public textlt::IModalContent {
public:
    FooterTestContent() {
        component_ = ftxui::Renderer([] { return ftxui::text("body"); });
    }

    ftxui::Element Render() override { return ftxui::text("body"); }
    ftxui::Component GetMainComponent() override { return component_; }
    std::string GetTitle() override { return "Footer test"; }
    ftxui::Element RenderTitle() override { return ftxui::text("Footer test"); }

private:
    ftxui::Component component_;
};

} // namespace

int main() {
    using namespace textlt;

    int translate_count = 0;
    int edit_count = 0;
    int stop_count = 0;
    int close_count = 0;
    AiQuickStatusSnapshot status;
    status.model_label = "Gemma test";
    status.language_label = "Russian -> Ukrainian";
    status.status_label = "Ready — llama.cpp";
    status.paragraph_label = "Hello · 42 characters";
    status.paragraph_available = true;
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

    status.ready = true;
    status.paragraph_available = false;
    status.paragraph_label = "The cursor is on an empty paragraph.";
    assert(content.HandleEvent(ftxui::Event::Character("1")));
    assert(translate_count == 1);
    status.paragraph_available = true;

    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    const ftxui::Event mouse_event = ftxui::Event::Mouse("", mouse);
    assert(!content.HandleEvent(mouse_event));
    assert(translate_count == 1);
    assert(edit_count == 1);
    assert(stop_count == 1);
    assert(close_count == 1);

    auto footer_content = std::make_shared<FooterTestContent>();
    textlt::ModalWindow footer_modal(footer_content, nullptr, [] {});
    footer_modal.SetHeaderCloseVisible(false);
    int footer_action = -1;
    std::vector<textlt::ModalWindow::FooterButton> footer_buttons;
    for (int index = 0; index < 6; ++index) {
        footer_buttons.push_back({
            "Action " + std::to_string(index + 1),
            [&, index] { footer_action = index; },
        });
    }
    footer_modal.SetFooterButtons(std::move(footer_buttons));
    for (int index = 0; index < 6; ++index) {
        assert(footer_modal.OnEvent(ftxui::Event::Tab));
    }
    assert(footer_modal.OnEvent(ftxui::Event::Return));
    assert(footer_action == 5);

    return 0;
}
