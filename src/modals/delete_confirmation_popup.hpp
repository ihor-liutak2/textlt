#pragma once

#include <string>

#include "ui_button.hpp"

namespace textlt {

inline ftxui::Element RenderDeleteConfirmationPopup(
    const Theme& theme, const std::string& title, const std::string& message,
    ftxui::Box& cancel_box, ftxui::Box& delete_box) {
    using namespace ftxui;
    const ButtonSpec confirm = ButtonSpecFromLabel(
        "Delete", ButtonRole::Danger, ButtonVariant::AccentEdges, ButtonSize::Compact);
    const ButtonSpec cancel = ButtonSpecFromLabel(
        "Cancel", ButtonRole::Cancel, ButtonVariant::Minimal, ButtonSize::Compact);
    return vbox({
        text(" " + title + " ") | bold | color(theme.button_danger),
        separator() | color(theme.modal_border),
        paragraph(message) | color(theme.modal_text_color),
        hbox({
            filler(),
            RenderModalFlatButton(theme, cancel) | reflect(cancel_box),
            text("  "),
            RenderModalFlatButton(theme, confirm) | reflect(delete_box),
            filler(),
        }),
    }) | size(WIDTH, EQUAL, 54) | borderStyled(HEAVY, theme.modal_border) |
        bgcolor(theme.modal_background) | clear_under;
}

} // namespace textlt
