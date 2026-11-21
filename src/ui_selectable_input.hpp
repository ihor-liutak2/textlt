#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

namespace textlt {

struct InputSelection {
    ftxui::Box box{1, 0, 1, 0};
    std::chrono::steady_clock::time_point last_click{};
    bool all_selected = false;
};

inline ftxui::Component MakeSelectableInput(
    std::string& value, int& cursor, std::string placeholder,
    ftxui::InputOption option, std::shared_ptr<InputSelection> selection) {
    using namespace ftxui;
    option.cursor_position = &cursor;
    const auto password = option.password;
    auto transform = std::move(option.transform);
    option.transform = [selection, transform = std::move(transform)](InputState state) {
        if (!state.focused) {
            selection->all_selected = false;
            selection->last_click = {};
        }
        Element element = transform ? transform(std::move(state)) : std::move(state.element);
        return selection->all_selected ? element | inverted : element;
    };
    auto input = Input(&value, std::move(placeholder), std::move(option));
    auto view = Renderer(input, [input, selection] {
        return input->Render() | reflect(selection->box);
    });
    return CatchEvent(view, [input, &value, &cursor, selection, password](Event event) {
        if (event.is_mouse()) {
            const auto& mouse = event.mouse();
            if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) return false;

            selection->all_selected = false;
            if (!selection->box.Contain(mouse.x, mouse.y)) {
                selection->last_click = {};
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - selection->last_click > std::chrono::milliseconds(500)) {
                selection->last_click = now;
                // The displayed mask need not have one cell per password character.
                if (!password() || value.empty()) return false;
            } else {
                selection->last_click = {};
                selection->all_selected = !value.empty();
            }
            cursor = static_cast<int>(value.size());
            input->TakeFocus();
            return true;
        }

        if (!selection->all_selected || !input->Focused()) return false;
        if (event.is_character() || event == Event::Backspace || event == Event::Delete) {
            selection->all_selected = false;
            value.clear();
            cursor = 0;
            return !event.is_character();
        }
        if (event == Event::ArrowLeft || event == Event::ArrowLeftCtrl ||
            event == Event::ArrowRight || event == Event::ArrowRightCtrl) {
            selection->all_selected = false;
            cursor = event == Event::ArrowLeft || event == Event::ArrowLeftCtrl
                ? 0 : static_cast<int>(value.size());
            return true;
        }
        if (event == Event::Home || event == Event::End || event == Event::Return) {
            selection->all_selected = false;
        }
        return false;
    });
}

} // namespace textlt
