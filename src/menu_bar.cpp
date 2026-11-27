#include "menu_bar.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

// #include "logger.hpp"

namespace textlt {

namespace {

bool IsLightTheme(const Theme& theme) {
    return theme.name.find("Light") != std::string::npos;
}

ftxui::Element MenuItemActiveStyle(const Theme& theme, ftxui::Element item) {
    return item |
           ftxui::bgcolor(theme.modal_selected_item_bg) |
           ftxui::color(theme.modal_selected_item_fg);
}

ftxui::Color MenuBorderColor(const Theme& theme) {
    if (IsLightTheme(theme)) {
        return ftxui::Color::RGB(70, 70, 70);
    }
    return theme.menu_foreground;
}

std::string ShortcutMenuLabel(std::string base_label, const std::string& shortcut) {
    if (shortcut.empty()) {
        return base_label;
    }
    if (base_label.empty()) {
        base_label = " ";
    }
    if (base_label.front() != ' ') {
        base_label.insert(base_label.begin(), ' ');
    }
    while (!base_label.empty() && base_label.back() == ' ') {
        base_label.pop_back();
    }
    base_label += " ";
    constexpr size_t kShortcutColumn = 20;
    if (base_label.size() < kShortcutColumn) {
        base_label.append(kShortcutColumn - base_label.size(), ' ');
    }
    base_label += "(" + shortcut + ") ";
    return base_label;
}

} // namespace

MenuBarComponent::MenuBarComponent(
    ActionCallback on_action,
    WorkspaceTabs workspace_tabs,
    const Theme* theme,
    ShortcutProvider shortcut_provider)
    : menu_entries_({
          "File",
          "Project",
          "Edit",
          "Options",
          "Notes",
          "Actions",
          "Connect",
          "Tools",
          "Help",
          "Exit",
      }),
      dropdown_items_({
          {
              {" New ", "file.new"},
              {" Open... ", "file.open"},
              {" Save As... ", "file.save_as"},
              {" Import... ", "file.import"},
              {" Recent Files ", "file.recent"},
              {" Close ", "file.close"},
              {" Close All ", "file.close_all"},
              {" Save ", "file.save"},
              {" Save All ", "file.save_all"},
              {" [ ] Add to Favorites ", "file.toggle_favorite"},
              {" Exit ", "app.exit"},
          },
          {
              {" Projects Sidebar ", "project.show"},
              {" Add Project... ", "project.add"},
              {" Add Current Folder as Project ", "project.add_current"},
              {" Remove Current Project ", "project.remove"},
              {" ───────────────────────── ", "", true},
              {" Remote Files... ", "remote.files"},
              {" Set/Change Connection... ", "project.assign_connection"},
              {" Edit Connection... ", "project.edit_connection"},
              {" Unlink Remote ", "project.unlink_connection"},
              {" ───────────────────────── ", "", true},
              {" Manage Connections... ", "remote.connections"},
          },
          {
              {" Undo ", "edit.undo"},
              {" Redo ", "edit.redo"},
              {" Select All ", "edit.select_all"},
              {" Cut ", "edit.cut"},
              {" Copy ", "edit.copy"},
              {" Paste ", "edit.paste"},
              {" Toggle Comment ", "edit.toggle_comment"},
              {" Toggle Case ", "edit.toggle_case"},
              {" Convert Indents: 4 -> 2 Spaces ", "edit.convert_indents_4_to_2"},
              {" Convert Indents: 2 -> 4 Spaces ", "edit.convert_indents_2_to_4"},
              {" Find... ", "edit.find"},
              {" Replace... ", "edit.replace"},
              {" Go to Line... ", "editor.go_to_line"},
              {" Search in Files...", "search.files"},
              {" Text Processors...", "text_processors.open"},
          },
          {
              {" Toggle Line Numbers ", "view.toggle_line_numbers"},
              {" Quick Commands ", "sidebar.ctrl_b_file_explorer"},
              {" Quick Text Formatting ", "text_processors.quick"},
              {" Text ", "docs.open"},
              {" Notes ", "notes.toggle"},
              {" Actions ", "actions.open"},
              {" Connect ", "connect.open"},
              {" Smart Word Wrap [ ] ", "editor.toggle_smart_word_wrap"},
              {" Syntax Highlighting [X] ", "editor.toggle_syntax_highlighting"},
              {" Auto Pairing [X] ", "editor.toggle_auto_pairing"},
              {" Smart Auto-Indent [X] ", "editor.toggle_auto_indent"},
              {" Tab Size: 4 spaces ", "editor.toggle_tab_size"},
              {" Convert Tabs to Spaces ", "editor.convert_tabs_to_spaces"},
              {" Theme... ", "theme.open"},
              {" View Layout... ", "view.layout"},
              {" Distraction Mode... ", "distraction.open_options"},
          },
          {
              {" New Note ", "notes.new"},
              {" Edit Note ", "notes.edit_selected"},
              {" Delete Note ", "notes.delete_selected"},
              {" Import Markdown... ", "notes.import_markdown"},
              {" ───────────────────────── ", "", true},
              {" Heading 1 ", "notes.heading_1"},
              {" Heading 2 ", "notes.heading_2"},
              {" Heading 3 ", "notes.heading_3"},
              {" ───────────────────────── ", "", true},
              {" Paragraph ", "notes.paragraph"},
              {" Bulleted List ", "notes.bullet_list"},
              {" Numbered List ", "notes.numbered_list"},
              {" Checklist ", "notes.checklist"},
              {" Send via Connect… ", "notes.share_connect"},
          },
          {
              {" New Group ", "actions.new_group"},
              {" Rename Group ", "actions.rename_group"},
              {" Delete Group ", "actions.delete_group"},
              {" ───────────────────────── ", "", true},
              {" New Page ", "actions.new_page"},
              {" Rename Page ", "actions.rename_page"},
              {" Delete Page ", "actions.delete_page"},
              {" Move Page Up ", "actions.move_page_up"},
              {" Move Page Down ", "actions.move_page_down"},
              {" ───────────────────────── ", "", true},
              {" Add Action ", "actions.add"},
              {" Import Actions... ", "actions.import"},
              {" Edit Action ", "actions.edit"},
              {" Delete Action ", "actions.delete"},
              {" Move Action Up ", "actions.move_up"},
              {" Move Action Down ", "actions.move_down"},
              {" Copy Selected Action ", "actions.copy"},
              {" Copy All Actions ", "actions.copy_all"},
              {" Send via Connect… ", "actions.share_connect"},
          },
          {
              {" TextLT Config... ", "connect.textlt_config"},
              {" LocalSend... ", "connect.localsend"},
              {" Diagnostics... ", "connect.diagnostics"},
          },
          {
              {" TTS ", "tts.open_modal"},
              {" TTS Settings ", "assistant.open_settings"},
              {" ───────────────────────── ", "", true},
              {" Quick AI ", "ai.quick"},
              {" AI Settings... ", "ai.open_settings"},
              {" ───────────────────────── ", "", true},
              {" Remote Files... ", "remote.files"},
              {" Remote Connections... ", "remote.connections"},
              {" ───────────────────────── ", "", true},
              {" Git... ", "git.open"},
              {" Git Settings... ", "git.settings"},
              {" ───────────────────────── ", "", true},
              {" Text Processors...", "text_processors.open"},
              {" Custom Processor Builder...", "custom_processor_builder.open"},
          },
          {
              {" About textlt ", "app.about"},
              {" Keyboard Shortcuts ", "app.help"},
              {" Theme Color Settings... ", "theme.colors"},
          },
          {
              {" Exit ", "app.exit"},
          },
      }),
      on_action_(std::move(on_action)),
      shortcut_provider_(std::move(shortcut_provider)),
      theme_(theme),
      workspace_tabs_(std::move(workspace_tabs)) {
    for (auto& menu : dropdown_items_) {
        for (DropdownItem& item : menu) {
            if (!item.separator) {
                item.base_label = item.label;
            }
        }
    }

    // Initialize the dropdown entries for the default menu item
    RefreshCurrentDropdownEntries();

    // Configure the top horizontal menu
    ftxui::MenuOption top_menu_option = ftxui::MenuOption::Horizontal();
    top_menu_option.on_enter = [this] { ActivateTopMenu(); };
    top_menu_option.on_change = [this] { ActivateTopMenu(); };
    
    // Custom styling for top menu items
    top_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        const size_t left_padding = !menu_entries_.empty() &&
            state.label == menu_entries_.front() ? 3 : 2;
        ftxui::Element item = ftxui::hbox({
            ftxui::text(std::string(left_padding, ' ')),
            ftxui::text(state.label),
            ftxui::text("  "),
        });
        if (state.focused || state.active) {
            return MenuItemActiveStyle(theme, std::move(item));
        }
        return item | ftxui::color(theme.menu_foreground);
    };
    top_menu_ = ftxui::Menu(&menu_entries_, &selected_menu_item_, top_menu_option);

    RebuildDropdownComponents();
    menu_container_ = ftxui::Container::Vertical({
        top_menu_,
        dropdown_menu_,
    });
    Add(menu_container_);
    const int notes_index = MenuIndex("Notes");
    if (notes_index >= 0) {
        notes_dropdown_items_ = dropdown_items_[static_cast<size_t>(notes_index)];
    }
    const int actions_index = MenuIndex("Actions");
    if (actions_index >= 0) actions_dropdown_items_ = dropdown_items_[static_cast<size_t>(actions_index)];
    const int connect_index = MenuIndex("Connect");
    if (connect_index >= 0) connect_dropdown_items_ = dropdown_items_[static_cast<size_t>(connect_index)];
    SetConnectMenuVisible(false);
    SetActionsMenuVisible(false);
    SetNotesMenuVisible(false);
}

void MenuBarComponent::RebuildDropdownComponents() {
    dropdown_buttons_.clear();
    dropdown_buttons_.reserve(current_dropdown_entries_.size());

    for (size_t index = 0; index < current_dropdown_entries_.size(); ++index) {
        if (IsCurrentSeparator(static_cast<int>(index))) {
            dropdown_buttons_.push_back(ftxui::Renderer([this, index] {
                const Theme& theme = theme_ ? *theme_ : FallbackTheme();
                return ftxui::text(current_dropdown_entries_[index]) |
                       ftxui::color(theme.menu_foreground) |
                       ftxui::dim;
            }));
            continue;
        }

        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        option.label = current_dropdown_entries_[index];
        option.on_click = [this, index] {
            selected_dropdown_item_ = static_cast<int>(index);
            HandleDropdownAction();
        };
        option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ftxui::Element item = ftxui::text(state.label);
            if (state.focused || state.active) {
                return MenuItemActiveStyle(theme, std::move(item));
            }
            return item | ftxui::color(theme.menu_foreground);
        };
        dropdown_buttons_.push_back(ftxui::Button(std::move(option)));
    }

    dropdown_menu_ = ftxui::Container::Vertical(dropdown_buttons_, &selected_dropdown_item_);
    if (menu_container_) {
        menu_container_->DetachAllChildren();
        menu_container_->Add(top_menu_);
        menu_container_->Add(dropdown_menu_);
    }
}


ftxui::Element MenuBarComponent::OnRender() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        text(""),
        hbox({
            // Scroll the menu with keyboard focus when the terminal is too narrow.
            top_menu_->Render() | xframe | xflex_shrink | reflect(top_menu_box_),
            filler(),
            workspace_tabs_.Render(theme),
        }) | bgcolor(theme.menu_background) | color(theme.menu_foreground),
        text(""),
    }) | reflect(menu_box_);
}

void MenuBarComponent::ClearHitboxes() {
    menu_box_ = top_menu_box_ = dropdown_box_ = {1, 0, 1, 0};
    workspace_tabs_.ClearHitboxes();
}

ftxui::Element MenuBarComponent::RenderDropdown() {
    using namespace ftxui;
    if (active_dropdown_ < 0) {
        return emptyElement();
    }

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    
    Element dropdown =
        dropdown_menu_->Render() |
        borderStyled(ftxui::LIGHT, MenuBorderColor(theme)) |
        bgcolor(theme.menu_background) |
        color(theme.menu_foreground) |
        reflect(dropdown_box_) |
        clear_under;

    return vbox({
        emptyElement() | size(HEIGHT, EQUAL, menu_box_.y_max + 1),
        hbox({
            emptyElement() | size(WIDTH, EQUAL, DropdownX()) | xflex_shrink,
            dropdown,
        }),
    });
}
   

/**
 * Processes events for the MenuBarComponent.
 * The logic prioritizes:
 * 1. ESC key to close active dropdowns.
 * 2. Keyboard navigation for the active dropdown (if open).
 * 3. Mouse interactions for selecting items or switching menus.
 * 4. Fallback to the top menu component for remaining events.
 */
bool MenuBarComponent::OnEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape && active_dropdown_ >= 0) {
        CloseDropdown();
        return true;
    }

    if (active_dropdown_ >= 0) {
        if (event == ftxui::Event::ArrowUp ||
            event == ftxui::Event::ArrowDown ||
            event == ftxui::Event::Return) {
            return dropdown_menu_->OnEvent(event);
        }
        if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
            CloseDropdown();
            top_menu_->TakeFocus();
            return top_menu_->OnEvent(event);
        }
    }

    if (!event.is_mouse()) {
        return top_menu_->OnEvent(event);
    }

    const auto& mouse = event.mouse();
    const bool pressed = mouse.button == ftxui::Mouse::Left &&
                         mouse.motion == ftxui::Mouse::Pressed;
    if (pressed && CaptureMouse(event) && workspace_tabs_.ActivateAt(mouse.x, mouse.y)) {
        CloseDropdown();
        return true;
    }

    const bool in_top_menu = top_menu_box_.Contain(mouse.x, mouse.y);
    if (active_dropdown_ >= 0) {
        if (dropdown_menu_->OnEvent(event)) {
            return true;
        }

        if (in_top_menu && top_menu_->OnEvent(event)) {
            if (pressed) {
                OpenDropdown(selected_menu_item_);
            }
            return true;
        }

        if (pressed && !in_top_menu && !dropdown_box_.Contain(mouse.x, mouse.y)) {
            CloseDropdown();
        }
        return true;
    }

    if (!in_top_menu || !top_menu_->OnEvent(event)) {
        return false;
    }
    if (pressed && active_dropdown_ < 0) {
        OpenDropdown(selected_menu_item_);
    }
    return true;
}

std::string MenuBarComponent::CommandIdAt(int menu_index, int item_index) const {
    if (menu_index < 0 || item_index < 0) {
        return "";
    }
    if (menu_index >= static_cast<int>(dropdown_items_.size())) {
        return "";
    }
    const auto& items = dropdown_items_[menu_index];
    if (item_index >= static_cast<int>(items.size())) {
        return "";
    }
    if (items[item_index].separator) {
        return "";
    }
    return items[item_index].command_id;
}

    
    
void MenuBarComponent::SetShortcutProvider(ShortcutProvider provider) {
    shortcut_provider_ = std::move(provider);
}

std::string MenuBarComponent::ComposeLabel(const DropdownItem& item) const {
    if (item.separator) {
        return item.label;
    }
    const std::string& base = item.base_label.empty() ? item.label : item.base_label;
    if (!shortcut_provider_ || item.command_id.empty()) {
        return base;
    }
    return ShortcutMenuLabel(base, shortcut_provider_(item.command_id));
}

void MenuBarComponent::RefreshShortcutLabels() {
    const auto refresh = [this](std::vector<DropdownItem>& items) {
        for (DropdownItem& item : items) {
            if (item.separator) {
                continue;
            }
            if (item.base_label.empty()) {
                item.base_label = item.label;
            }
            item.label = ComposeLabel(item);
        }
    };

    for (auto& menu : dropdown_items_) {
        refresh(menu);
    }
    refresh(notes_dropdown_items_);
    refresh(actions_dropdown_items_);
    RefreshCurrentDropdownEntries();
}

void MenuBarComponent::SetFileMenuLabels(bool is_favorite, bool comparing) {
    SetItemLabel("file.close", comparing ? " Close Git Compare " : " Close ");
    SetItemLabel("file.toggle_favorite", is_favorite
        ? " [X] Remove from Favorites "
        : " [ ] Add to Favorites ");
    RefreshCurrentDropdownEntries();
}

void MenuBarComponent::SetNotesMenuVisible(bool visible) {
    SetContextualMenu("Notes", notes_dropdown_items_, notes_menu_visible_, visible);
}

void MenuBarComponent::SetActionsMenuVisible(bool visible) {
    SetContextualMenu("Actions", actions_dropdown_items_, actions_menu_visible_, visible);
}

void MenuBarComponent::SetConnectMenuVisible(bool visible) {
    SetContextualMenu("Connect", connect_dropdown_items_, connect_menu_visible_, visible);
}

void MenuBarComponent::SetContextualMenu(const std::string& label,
                                         const std::vector<DropdownItem>& items,
                                         bool& current, bool visible) {
    if (current == visible) return;
    CloseDropdown();
    const int index = MenuIndex(label);
    if (visible && index < 0) {
        const int tools = MenuIndex("Tools");
        const auto insertion = tools < 0 ? static_cast<std::ptrdiff_t>(menu_entries_.size()) : tools;
        menu_entries_.insert(menu_entries_.begin() + insertion, label);
        dropdown_items_.insert(dropdown_items_.begin() + insertion, items);
    } else if (!visible && index >= 0) {
        menu_entries_.erase(menu_entries_.begin() + index);
        dropdown_items_.erase(dropdown_items_.begin() + index);
    }
    current = visible;
    selected_menu_item_ = std::clamp(selected_menu_item_, 0,
        std::max(0, static_cast<int>(menu_entries_.size()) - 1));
    selected_dropdown_item_ = 0;
    RefreshCurrentDropdownEntries();
}

void MenuBarComponent::SetNotesTrashSelected(bool trash_selected) {
    if (notes_trash_selected_ == trash_selected) {
        return;
    }
    notes_trash_selected_ = trash_selected;
    SetItemLabel("notes.delete_selected", trash_selected
        ? " Delete Permanently "
        : " Delete Note ");
    RefreshCurrentDropdownEntries();
}

void MenuBarComponent::SetOptionLabels(
    bool smart_word_wrap,
    bool syntax_highlighting,
    bool auto_pairing,
    bool auto_indent,
    int tab_size) {
    SetItemLabel("editor.toggle_smart_word_wrap", smart_word_wrap
        ? " Smart Word Wrap [X] "
        : " Smart Word Wrap [ ] ");
    SetItemLabel("editor.toggle_syntax_highlighting", syntax_highlighting
        ? " Syntax Highlighting [X] "
        : " Syntax Highlighting [ ] ");
    SetItemLabel("editor.toggle_auto_pairing", auto_pairing
        ? " Auto Pairing [X] "
        : " Auto Pairing [ ] ");
    SetItemLabel("editor.toggle_auto_indent", auto_indent
        ? " Smart Auto-Indent [X] "
        : " Smart Auto-Indent [ ] ");
    SetItemLabel("editor.toggle_tab_size", " Tab Size: " + std::to_string(tab_size) + " spaces ");
    RefreshCurrentDropdownEntries();
}

bool MenuBarComponent::SetItemLabel(const std::string& command_id, std::string label) {
    bool updated = false;
    const auto apply_label = [this, &label, &command_id](DropdownItem& item) {
        if (item.command_id != command_id) {
            return false;
        }
        item.base_label = label;
        item.label = ComposeLabel(item);
        return true;
    };
    for (auto& menu : dropdown_items_) {
        for (DropdownItem& item : menu) {
            updated = apply_label(item) || updated;
        }
    }
    // The Notes menu is stored separately so it can be removed from the menu
    // bar outside the notes workspace. Keep its labels current even while the
    // menu is hidden.
    for (DropdownItem& item : notes_dropdown_items_) {
        updated = apply_label(item) || updated;
    }
    return updated;
}

int MenuBarComponent::MenuIndex(const std::string& label) const {
    const auto iter = std::find(menu_entries_.begin(), menu_entries_.end(), label);
    return iter == menu_entries_.end()
        ? -1
        : static_cast<int>(std::distance(menu_entries_.begin(), iter));
}

void MenuBarComponent::CloseDropdown() {
    active_dropdown_ = -1;
}

void MenuBarComponent::OpenDropdown(int menu_index) {
    if (menu_index < 0 || menu_index >= static_cast<int>(dropdown_items_.size())) {
        CloseDropdown();
        return;
    }

    active_dropdown_ = menu_index;
    selected_menu_item_ = menu_index;
    selected_dropdown_item_ = 0;
    RefreshCurrentDropdownEntries();
    dropdown_menu_->TakeFocus();
}

void MenuBarComponent::ActivateTopMenu() {
    OpenDropdown(selected_menu_item_);
}

void MenuBarComponent::HandleDropdownAction() {
    if (active_dropdown_ < 0 ||
        selected_dropdown_item_ < 0 ||
        selected_dropdown_item_ >= static_cast<int>(current_dropdown_entries_.size())) {
        return;
    }

    if (IsCurrentSeparator(selected_dropdown_item_)) {
        return;
    }

    if (on_action_) {
        on_action_(active_dropdown_, selected_dropdown_item_);
    }
    CloseDropdown();
}

void MenuBarComponent::RefreshCurrentDropdownEntries() {
    if (dropdown_items_.empty()) {
        current_dropdown_entries_.clear();
        current_dropdown_command_ids_.clear();
        current_dropdown_separators_.clear();
        return;
    }

    const int menu_index = std::clamp(
        active_dropdown_ >= 0 ? active_dropdown_ : selected_menu_item_,
        0,
        static_cast<int>(dropdown_items_.size()) - 1);
    const std::vector<DropdownItem>& next_items = dropdown_items_[menu_index];

    std::vector<std::string> next_entries;
    std::vector<std::string> next_command_ids;
    std::vector<bool> next_separators;
    next_entries.reserve(next_items.size());
    next_command_ids.reserve(next_items.size());
    next_separators.reserve(next_items.size());
    for (const DropdownItem& item : next_items) {
        next_entries.push_back(item.label);
        next_command_ids.push_back(item.command_id);
        next_separators.push_back(item.separator);
    }

    if (current_dropdown_entries_ == next_entries &&
        current_dropdown_command_ids_ == next_command_ids &&
        current_dropdown_separators_ == next_separators) {
        return;
    }
    current_dropdown_entries_ = std::move(next_entries);
    current_dropdown_command_ids_ = std::move(next_command_ids);
    current_dropdown_separators_ = std::move(next_separators);

    if (selected_dropdown_item_ >= static_cast<int>(current_dropdown_entries_.size())) {
        selected_dropdown_item_ = 0;
    }
    if (IsCurrentSeparator(selected_dropdown_item_)) {
        selected_dropdown_item_ = 0;
        while (selected_dropdown_item_ < static_cast<int>(current_dropdown_separators_.size()) &&
               current_dropdown_separators_[selected_dropdown_item_]) {
            ++selected_dropdown_item_;
        }
        if (selected_dropdown_item_ >= static_cast<int>(current_dropdown_separators_.size())) {
            selected_dropdown_item_ = 0;
        }
    }
    if (dropdown_menu_) {
        RebuildDropdownComponents();
    }
}

bool MenuBarComponent::IsCurrentSeparator(int item_index) const {
    return item_index >= 0 &&
           item_index < static_cast<int>(current_dropdown_separators_.size()) &&
           current_dropdown_separators_[item_index];
}


// Returns the X-coordinate for the current active dropdown
int MenuBarComponent::DropdownX() const {
    int dropdown_x = top_menu_box_.x_min;
    for (int i = 0; i < active_dropdown_ && i < static_cast<int>(menu_entries_.size()); ++i) {
        dropdown_x += static_cast<int>(menu_entries_[i].size()) + (i == 0 ? 6 : 5);
    }
    return dropdown_x;
}

} // namespace textlt
