#include <cstdlib>
#include <iostream>
#include <string>

#include "menu_bar.hpp"

namespace textlt {

class MenuBarTestAccess {
public:
    static int MenuIndex(const MenuBarComponent& component, const std::string& label) {
        return component.MenuIndex(label);
    }
    static const MenuBarComponent::DropdownItem& ItemAt(
        const MenuBarComponent& component, int menu_index, int item_index) {
        return component.dropdown_items_[static_cast<size_t>(menu_index)]
            [static_cast<size_t>(item_index)];
    }
    static std::string CurrentEntry(
        const MenuBarComponent& component, int item_index) {
        if (item_index < 0 ||
            item_index >= static_cast<int>(component.current_dropdown_entries_.size())) {
            return {};
        }
        return component.current_dropdown_entries_[static_cast<size_t>(item_index)];
    }
};

} // namespace textlt

namespace {

void Expect(bool value, const std::string& message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using textlt::MenuBarComponent;
    using textlt::MenuBarTestAccess;

    MenuBarComponent menu_bar([](int, int) {}, nullptr);

    Expect(MenuBarTestAccess::MenuIndex(menu_bar, " Format ") < 0,
        "The Format menu must no longer be part of the menu bar.");
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, " Notes ") < 0,
        "The Notes menu should be hidden outside the notes workspace.");
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, " Actions ") < 0,
        "The Actions menu should be hidden outside the actions workspace.");
    menu_bar.SetActionsMenuVisible(true);
    const int actions_index = MenuBarTestAccess::MenuIndex(menu_bar, " Actions ");
    Expect(actions_index >= 0, "The Actions menu was not inserted.");
    Expect(menu_bar.CommandIdAt(actions_index, 0) == "actions.new_group",
        "The Actions menu should expose New Group first.");
    Expect(menu_bar.CommandIdAt(actions_index, 11) == "actions.import",
        "The Actions menu should expose file import after Add Action.");
    menu_bar.SetActionsMenuVisible(false);

    menu_bar.SetNotesMenuVisible(true);
    const int notes_index = MenuBarTestAccess::MenuIndex(menu_bar, " Notes ");
    Expect(notes_index >= 0, "The Notes menu was not inserted.");
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, " Format ") < 0,
        "Showing the notes workspace must not restore the Format menu.");

    Expect(menu_bar.CommandIdAt(notes_index, 0) == "notes.new",
        "The Notes menu should expose New Note first.");
    Expect(menu_bar.CommandIdAt(notes_index, 1) == "notes.edit_selected",
        "The Notes menu should expose Edit Note.");
    Expect(menu_bar.CommandIdAt(notes_index, 2) == "notes.delete_selected",
        "The Notes menu should expose Delete Note.");
    Expect(MenuBarTestAccess::ItemAt(menu_bar, notes_index, 2).label == " Delete Note ",
        "The delete action should be labelled Delete Note outside Trash.");

    menu_bar.OpenDropdown(notes_index);
    Expect(menu_bar.IsDropdownOpen(), "The Notes dropdown did not open.");
    Expect(MenuBarTestAccess::CurrentEntry(menu_bar, 2) == " Delete Note ",
        "The open Notes menu did not show the Delete Note label.");

    menu_bar.SetNotesTrashSelected(true);
    Expect(MenuBarTestAccess::ItemAt(menu_bar, notes_index, 2).label ==
            " Delete Permanently ",
        "Selecting Trash should relabel the delete action as Delete Permanently.");
    Expect(MenuBarTestAccess::CurrentEntry(menu_bar, 2) == " Delete Permanently ",
        "The open Notes menu did not refresh the permanent-delete label.");

    menu_bar.SetNotesTrashSelected(false);
    Expect(MenuBarTestAccess::ItemAt(menu_bar, notes_index, 2).label == " Delete Note ",
        "Leaving Trash should restore the Delete Note label.");

    // The label must also survive while the menu is hidden, otherwise the
    // menu would show a stale action when entering Trash later.
    menu_bar.SetNotesMenuVisible(false);
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, " Notes ") < 0,
        "The Notes menu was not removed outside the notes workspace.");
    menu_bar.SetNotesTrashSelected(true);
    menu_bar.SetNotesMenuVisible(true);
    const int reopened_index = MenuBarTestAccess::MenuIndex(menu_bar, " Notes ");
    Expect(reopened_index >= 0, "The Notes menu was not restored.");
    Expect(MenuBarTestAccess::ItemAt(menu_bar, reopened_index, 2).label ==
            " Delete Permanently ",
        "The delete label was lost while the Notes menu was hidden.");

    return 0;
}
