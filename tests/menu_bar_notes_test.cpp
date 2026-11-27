#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/screen.hpp"
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
    std::cerr << "Check: " << message << '\n';
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void TestWorkspaceSwitcher() {
    using namespace ftxui;
    using namespace textlt;

    const Theme& theme = FallbackTheme();
    auto active = WorkspaceMode::Documents;
    int switches = 0;
    WorkspaceTabs::Callbacks callbacks;
    for (std::size_t index = 0; index < callbacks.size(); ++index) {
        callbacks[index] = [&, index] {
            active = static_cast<WorkspaceMode>(index);
            ++switches;
        };
    }
    MenuBarComponent menu([](int, int) {}, WorkspaceTabs(active, callbacks), &theme);
    constexpr std::array labels{"Text", "Notes", "Actions", "Connect"};
    constexpr std::array offsets{30, 24, 17, 8};
    const auto click = [&](int x, int y) {
        Mouse mouse{};
        mouse.x = x;
        mouse.y = y;
        mouse.button = Mouse::Left;
        mouse.motion = Mouse::Pressed;
        return menu.OnEvent(Event::Mouse("", mouse));
    };
    const auto check_render = [&](int width) {
        menu.SetNotesMenuVisible(active == WorkspaceMode::Notes);
        menu.SetActionsMenuVisible(active == WorkspaceMode::Actions);
        menu.SetConnectMenuVisible(active == WorkspaceMode::Connect);
        Screen screen(width, 3);
        Render(screen, menu.Render());
        std::string row;
        bool blank_padding = true;
        const auto is_blank = [](const std::string& cell) {
            return cell.empty() || cell == " ";
        };
        for (int x = 0; x < width; ++x) {
            const auto& cell = screen.at(x, 1);
            row += cell.empty() ? " " : cell;
            blank_padding = blank_padding && is_blank(screen.at(x, 0)) && is_blank(screen.at(x, 2));
        }
        Expect(blank_padding, "The menu should have one blank row on either side.");
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const int x = width - offsets[index];
            Expect(row.find(labels[index], x) == static_cast<std::size_t>(x),
                "Workspace buttons should stay in order at the right edge.");
            const auto& cell = screen.CellAt(x, 1);
            const bool selected = index == static_cast<std::size_t>(active);
            Expect(cell.background_color ==
                       (selected ? theme.modal_selected_item_bg : theme.menu_background) &&
                   cell.foreground_color ==
                       (selected ? theme.modal_selected_item_fg : theme.menu_foreground),
                "Only the active workspace button should have the theme selection colors.");
        }
        if (width == 80) {
            Expect(row.find("File Project Edit Options") == 0 &&
                       row.find("│") == std::string::npos,
                "The menus should be left-aligned and have no separators.");
        }
    };
    check_render(80);
    Expect(!click(80 - offsets[1], 0) && !click(80 - offsets[1], 2),
        "The blank menu rows should not activate workspace buttons.");
    for (std::size_t index = 1; index < labels.size(); ++index) {
        Expect(click(80 - offsets[index], 1) && active == static_cast<WorkspaceMode>(index),
            "A workspace button should activate its corresponding workspace.");
        check_render(80);
    }
    Expect(click(80 - offsets[3], 1) && switches == 3,
        "Clicking the active workspace should preserve its state.");
    check_render(32);
    Expect(click(32 - offsets[0], 1) && active == WorkspaceMode::Documents,
        "Workspace hitboxes should follow the buttons on resize.");
    active = WorkspaceMode::Actions;
    check_render(32);
    menu.ClearHitboxes();
    Expect(!click(32 - offsets[0], 1), "A hidden menu should not receive workspace clicks.");
}

} // namespace

int main() {
    using textlt::MenuBarComponent;
    using textlt::MenuBarTestAccess;

    TestWorkspaceSwitcher();

    const auto workspace = textlt::WorkspaceMode::Documents;
    MenuBarComponent menu_bar([](int, int) {}, textlt::WorkspaceTabs(workspace, {}));

    menu_bar.SetShortcutProvider([](const std::string& command_id) {
        return command_id == "edit.undo" ? std::string("Ctrl+U") : std::string();
    });
    menu_bar.RefreshShortcutLabels();
    const int file_index = MenuBarTestAccess::MenuIndex(menu_bar, "File");
    Expect(file_index >= 0, "The File menu should be present.");
    Expect(menu_bar.CommandIdAt(file_index, 1) == "file.open" &&
            menu_bar.CommandIdAt(file_index, 2) == "file.save_as",
        "File should use a single Open item before Save As.");

    const int edit_index = MenuBarTestAccess::MenuIndex(menu_bar, "Edit");
    Expect(edit_index >= 0, "The Edit menu should be present.");
    Expect(MenuBarTestAccess::ItemAt(menu_bar, edit_index, 0).label.find("(Ctrl+U)") !=
            std::string::npos,
        "The Edit menu should render the effective shortcut from the shortcut provider.");

    Expect(MenuBarTestAccess::MenuIndex(menu_bar, "Format") < 0,
        "The Format menu must no longer be part of the menu bar.");
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, "Notes") < 0,
        "The Notes menu should be hidden outside the notes workspace.");
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, "Actions") < 0,
        "The Actions menu should be hidden outside the actions workspace.");
    const int options_index = MenuBarTestAccess::MenuIndex(menu_bar, "Options");
    Expect(options_index >= 0, "The Options menu should always be present.");
    Expect(menu_bar.CommandIdAt(options_index, 1) == "sidebar.ctrl_b_file_explorer" &&
        menu_bar.CommandIdAt(options_index, 2) == "text_processors.quick",
        "Quick Text Formatting should immediately follow Quick Commands.");
    Expect(menu_bar.CommandIdAt(options_index, 3) == "docs.open",
        "The Options menu should expose the Text view above Notes.");
    Expect(menu_bar.CommandIdAt(options_index, 4) == "notes.toggle",
        "The Options menu should keep Notes right after Text.");
    Expect(menu_bar.CommandIdAt(options_index, 5) == "actions.open",
        "The Options menu should expose the Actions view after Notes.");
    menu_bar.SetActionsMenuVisible(true);
    const int actions_index = MenuBarTestAccess::MenuIndex(menu_bar, "Actions");
    Expect(actions_index >= 0, "The Actions menu was not inserted.");
    Expect(menu_bar.CommandIdAt(actions_index, 0) == "actions.new_group",
        "The Actions menu should expose New Group first.");
    Expect(menu_bar.CommandIdAt(actions_index, 11) == "actions.import",
        "The Actions menu should expose file import after Add Action.");
    menu_bar.SetActionsMenuVisible(false);

    menu_bar.SetNotesMenuVisible(true);
    const int notes_index = MenuBarTestAccess::MenuIndex(menu_bar, "Notes");
    Expect(notes_index >= 0, "The Notes menu was not inserted.");
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, "Format") < 0,
        "Showing the notes workspace must not restore the Format menu.");

    Expect(menu_bar.CommandIdAt(notes_index, 0) == "notes.new",
        "The Notes menu should expose New Note first.");
    Expect(menu_bar.CommandIdAt(notes_index, 1) == "notes.edit_selected",
        "The Notes menu should expose Edit Note.");
    Expect(menu_bar.CommandIdAt(notes_index, 2) == "notes.delete_selected",
        "The Notes menu should expose Delete Note.");
    Expect(MenuBarTestAccess::ItemAt(menu_bar, notes_index, 2).label == " Delete Note ",
        "The delete action should be labelled Delete Note outside Trash.");
    Expect(menu_bar.CommandIdAt(notes_index, 3) == "notes.import_markdown",
        "The Notes menu should show Import Markdown with the note actions.");
    Expect(menu_bar.CommandIdAt(notes_index, 5) == "notes.heading_1",
        "The Notes menu should expose Heading 1 after the first separator.");
    Expect(menu_bar.CommandIdAt(notes_index, 6) == "notes.heading_2",
        "The Notes menu should expose Heading 2.");
    Expect(menu_bar.CommandIdAt(notes_index, 7) == "notes.heading_3",
        "The Notes menu should expose Heading 3.");

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
    Expect(MenuBarTestAccess::MenuIndex(menu_bar, "Notes") < 0,
        "The Notes menu was not removed outside the notes workspace.");
    menu_bar.SetNotesTrashSelected(true);
    menu_bar.SetNotesMenuVisible(true);
    const int reopened_index = MenuBarTestAccess::MenuIndex(menu_bar, "Notes");
    Expect(reopened_index >= 0, "The Notes menu was not restored.");
    Expect(MenuBarTestAccess::ItemAt(menu_bar, reopened_index, 2).label ==
            " Delete Permanently ",
        "The delete label was lost while the Notes menu was hidden.");

    return 0;
}
