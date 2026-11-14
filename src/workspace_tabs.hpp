#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace textlt {

enum class WorkspaceTab { Documents, Notes, Actions };

inline ftxui::Element RenderWorkspaceTabs(
    const Theme& theme,
    WorkspaceTab active,
    ftxui::Box& documents_box,
    ftxui::Box& notes_box,
    ftxui::Box& actions_box) {
    auto tab = [&](const char* label, WorkspaceTab tab_value, ftxui::Box& box) {
        const bool selected = active == tab_value;
        ftxui::Element element = ftxui::text(std::string(" ") + label + " ") |
            ftxui::color(selected ? theme.button_selected_fg : theme.modal_text_color);
        if (selected) {
            element = element | ftxui::bgcolor(theme.button_selected_bg) | ftxui::bold;
        }
        return element | ftxui::reflect(box);
    };
    return ftxui::hbox({
        tab("Docs", WorkspaceTab::Documents, documents_box),
        tab("Notes", WorkspaceTab::Notes, notes_box),
        tab("Actions", WorkspaceTab::Actions, actions_box),
    });
}

} // namespace textlt
