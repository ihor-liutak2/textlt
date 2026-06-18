#include "menu_bar.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

// #include "logger.hpp"

namespace textlt {

MenuBarComponent::MenuBarComponent(

    ActionCallback on_action,
    const Theme* theme)
    : menu_entries_({
          " File ",
          " Edit ",
          " Options ",
          " Help ",
          " Exit ",
      }),
      dropdown_entries_({
          {" New ", " Open ", " Save ", " Save As ", " [ ] Add to Favorites ", " Exit "},
          {
              " Undo               (Ctrl+Z) ",
              " Redo               (Ctrl+Y) ",
              " Select All         (Ctrl+A) ",
              " Cut                (Ctrl+X) ",
              " Copy               (Ctrl+C) ",
              " Paste              (Ctrl+V) ",
              " Toggle Comment     (Ctrl+/) ",
              " Toggle Case        (Ctrl+T) ",
              " Convert Indents: 4 -> 2 Spaces ",
              " Convert Indents: 2 -> 4 Spaces ",
              " Find...            (Ctrl+F) ",
              " Replace...         (Ctrl+R) ",
          },
          {
              " Toggle Line Numbers ",
              " Toggle File Explorer ",
              " Smart Word Wrap [ ] ",
              " Syntax Highlighting [X] ",
              " Auto Pairing [X] ",
              " Smart Auto-Indent [X] ",
              " Tab Size: 4 spaces ",
              " Convert Tabs to Spaces ",
              " Theme... ",
          },
          {" About textlt ", " Keyboard Shortcuts "},
          {" Exit "},
      }),
      on_action_(std::move(on_action)),
      theme_(theme) {
    
    // Initialize the dropdown entries for the default menu item
    RefreshCurrentDropdownEntries();

    // Configure the top horizontal menu
    ftxui::MenuOption top_menu_option = ftxui::MenuOption::Toggle();
    top_menu_option.on_enter = [this] { ActivateTopMenu(); };
    top_menu_option.on_change = [this] { ActivateTopMenu(); };
    
    // Custom styling for top menu items
    top_menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(state.label);
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.menu_foreground) |
                ftxui::color(theme.menu_background);
        }
        return item | ftxui::color(theme.menu_foreground);
    };
    top_menu_ = ftxui::Menu(&menu_entries_, &selected_menu_item_, top_menu_option);

    // Configure the vertical dropdown menu
    ftxui::MenuOption dropdown_option = ftxui::MenuOption::Vertical();
    dropdown_option.on_enter = [this] { HandleDropdownAction(); };
    
    // Custom styling for dropdown items
    dropdown_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(state.label);
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.menu_foreground) |
                ftxui::color(theme.menu_background);
        }
        return item | ftxui::color(theme.menu_foreground);
    };
    dropdown_menu_ =
        ftxui::Menu(&current_dropdown_entries_, &selected_dropdown_item_, dropdown_option);
          
   auto container = ftxui::Container::Vertical({
        top_menu_,
        dropdown_menu_,
    });
          
     Add(container);
}

ftxui::Element MenuBarComponent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    // Ensure we have at least one box allocated
    if (menu_boxes_.empty()) {
        menu_boxes_.resize(1);
    }

    return top_menu_->Render() |
           bgcolor(theme.menu_background) |
           color(theme.menu_foreground) |
           reflect(menu_boxes_[0]);
}

ftxui::Element MenuBarComponent::RenderDropdown() {
    using namespace ftxui;
    if (active_dropdown_ < 0) {
        return emptyElement();
    }

    // Safety: ensure menu_boxes_ exists
    if (menu_boxes_.empty()) {
        menu_boxes_.resize(1);
    }

    // Ensure dropdown_boxes_ has enough capacity
    if (static_cast<int>(dropdown_boxes_.size()) <= active_dropdown_) {
        dropdown_boxes_.resize(active_dropdown_ + 1);
    }

    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    
    Element dropdown =
        dropdown_menu_->Render() |
        border |
        bgcolor(theme.menu_background) |
        color(theme.menu_foreground) |
        reflect(dropdown_boxes_[active_dropdown_]) |
        clear_under;

    return vbox({
        // Safe access to menu_boxes_[0]
        emptyElement() | size(HEIGHT, EQUAL, menu_boxes_[0].y_max + 1),
        hbox({
            emptyElement() | size(WIDTH, EQUAL, std::max(0, DropdownX())),
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
    // 1. ESC handling: Immediately close any active dropdown menu.
    if (event == ftxui::Event::Escape && active_dropdown_ >= 0) {
        CloseDropdown();
        return true;
    }

    // 2. Keyboard handling: Process navigation events when a dropdown is open.
    // By placing this before mouse handling, we ensure arrow keys are not 
    // inadvertently blocked or consumed by mouse logic.
    // 2. Keyboard handling: Process navigation events
    if (active_dropdown_ >= 0) {
        // Vertical navigation and selection for the dropdown menu.
        if (event == ftxui::Event::ArrowUp || 
            event == ftxui::Event::ArrowDown || 
            event == ftxui::Event::Return) {
            
            bool processed = dropdown_menu_->OnEvent(event);
            return processed;
        }
        
        // Horizontal navigation: Switch between main top menu items.
        // We close the current dropdown, shift focus back to the top menu, 
        // and let the top menu process the left/right arrow.
        if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::ArrowRight) {
            CloseDropdown();
            top_menu_->TakeFocus();
            return top_menu_->OnEvent(event);
        }
    }

    // 3. Mouse event handling.
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        
        // Handle logic while a dropdown is active.
        if (active_dropdown_ >= 0) {
            int item_index = DropdownItemAt(mouse.x, mouse.y);

            // A. Item selection: Trigger only on button release to prevent accidental triggers.
            if (item_index >= 0 && mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Released) {
                selected_dropdown_item_ = item_index;
                HandleDropdownAction();
                return true;
            }

            // B. Navigation/Closing: If left button is pressed.
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                int new_menu = MenuIndexAt(mouse.x, mouse.y);
                
                // Switch to another top menu header if clicked.
                if (new_menu >= 0 && new_menu != active_dropdown_) {
                    OpenDropdown(new_menu);
                    return true;
                } 
                
                // Close the dropdown if clicking outside both the dropdown and the top menu bar.
                bool in_top_menu = (!menu_boxes_.empty() && menu_boxes_[0].Contain(mouse.x, mouse.y));
                if (item_index < 0 && !in_top_menu) {
                    CloseDropdown();
                }
            }
            // Consume mouse events to maintain isolation of the dropdown interface.
            return true; 
        }

        // C. Default: Handle opening a menu if no dropdown is active.
        if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
            int menu_index = MenuIndexAt(mouse.x, mouse.y);
            if (menu_index >= 0) {
                OpenDropdown(menu_index);
                return true;
            }
        }
    }
    
    // 4. Fallback: Delegate unhandled events to the top horizontal menu.
    return top_menu_->OnEvent(event);
}
    
    
void MenuBarComponent::SetFileFavoriteLabel(bool is_favorite) {
    if (dropdown_entries_.empty() || dropdown_entries_[0].size() <= 4) {
        return;
    }

    dropdown_entries_[0][4] = is_favorite
        ? " [X] Add to Favorites "
        : " [ ] Add to Favorites ";
    RefreshCurrentDropdownEntries();
}

void MenuBarComponent::SetOptionLabels(
    bool smart_word_wrap,
    bool syntax_highlighting,
    bool auto_pairing,
    bool auto_indent,
    int tab_size) {
    if (dropdown_entries_.size() <= 2 || dropdown_entries_[2].size() <= 6) {
        return;
    }

    dropdown_entries_[2][2] = smart_word_wrap
        ? " Smart Word Wrap [X] "
        : " Smart Word Wrap [ ] ";
    dropdown_entries_[2][3] = syntax_highlighting
        ? " Syntax Highlighting [X] "
        : " Syntax Highlighting [ ] ";
    dropdown_entries_[2][4] = auto_pairing
        ? " Auto Pairing [X] "
        : " Auto Pairing [ ] ";
    dropdown_entries_[2][5] = auto_indent
        ? " Smart Auto-Indent [X] "
        : " Smart Auto-Indent [ ] ";
    dropdown_entries_[2][6] =
        " Tab Size: " + std::to_string(tab_size) + " spaces ";
    RefreshCurrentDropdownEntries();
}

void MenuBarComponent::CloseDropdown() {
    active_dropdown_ = -1;
}

void MenuBarComponent::OpenDropdown(int menu_index) {
    if (menu_index < 0 || menu_index >= static_cast<int>(dropdown_entries_.size())) {
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

    if (on_action_) {
        on_action_(active_dropdown_, selected_dropdown_item_);
    }
    CloseDropdown();
}

void MenuBarComponent::RefreshCurrentDropdownEntries() {
    if (dropdown_entries_.empty()) {
        current_dropdown_entries_.clear();
        return;
    }

    const int menu_index = std::clamp(
        active_dropdown_ >= 0 ? active_dropdown_ : selected_menu_item_,
        0,
        static_cast<int>(dropdown_entries_.size()) - 1);
    current_dropdown_entries_ = dropdown_entries_[menu_index];
    if (selected_dropdown_item_ >= static_cast<int>(current_dropdown_entries_.size())) {
        selected_dropdown_item_ = 0;
    }
}

// Returns the X-coordinate for the current active dropdown
int MenuBarComponent::DropdownX() const {
    // Ensure we have at least one menu box to reference
    if (menu_boxes_.empty()) {
        return 0;
    }

    int dropdown_x = menu_boxes_[0].x_min;
    for (int i = 0; i < active_dropdown_ && i < static_cast<int>(menu_entries_.size()); ++i) {
        dropdown_x += static_cast<int>(menu_entries_[i].size()) + 1;
    }
    return dropdown_x;
}


/**
 * Determines the index of the main menu item under the mouse cursor.
 * Uses strict vertical (Y) row checking to ensure the cursor does not 
 * trigger main menu switching while inside a dropdown menu.
 */
int MenuBarComponent::MenuIndexAt(int x, int y) const {
    // 1. Safety check to ensure coordinate boxes are initialized
    if (menu_boxes_.empty()) {
        return -1;
    }

    const auto& box = menu_boxes_[0];

    // 2. STRICT VERTICAL BOUNDARY CHECK:
    // The main menu headers exist only on the single row 'box.y_min'.
    // If y is not equal to this row, the mouse is either inside a dropdown 
    // or elsewhere on the screen, so it cannot be a click on a header.
    bool within_header_row = (y == box.y_min); 
    
    if (!within_header_row) {
        return -1;
    }

    // 3. Search for the menu index along the horizontal (X) axis
    int item_x = box.x_min;
    for (size_t i = 0; i < menu_entries_.size(); ++i) {
        // Calculate the width of the current menu item
        const int item_width = static_cast<int>(menu_entries_[i].size());
        
        // Check if the cursor is within the bounds of this specific menu item
        if (x >= item_x && x < item_x + item_width) {
            return static_cast<int>(i);
        }
        
        // Move to the next item (adding 1 for the spacing between menu entries)
        item_x += item_width + 1;
    }
    
    // If we are in the correct row but clicked between items
    return -1;
}

int MenuBarComponent::DropdownItemAt(int x, int y) const {
    // 1. Basic safety check
    if (active_dropdown_ < 0 || active_dropdown_ >= static_cast<int>(dropdown_boxes_.size())) {
        return -1;
    }

    const auto& box = dropdown_boxes_[active_dropdown_];
    
    // 2. Capture boundary state
    bool inside = box.Contain(x, y);

    if (inside) {
        // The border is 1 unit thick, so the first item is at y_min + 1
        int item_index = y - box.y_min - 1;
        
        // 4. Validate index
        bool valid_index = (item_index >= 0 && item_index < static_cast<int>(current_dropdown_entries_.size()));
        
        if (valid_index) {
            return item_index;
        } 
    } 
    
    return -1;
}

} // namespace textlt
