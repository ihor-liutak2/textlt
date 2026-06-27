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
          " AI ",
          " Git ",
          " Help ",
          " Exit ",
      }),
      dropdown_entries_({
          {
              " New ",
              " Files... ",
              " Open...            (Ctrl+O) ",
              " Save As... ",
              " Import... ",
              " Export... ",
              " Recent Files ",
              " Close              (Alt+W) ",
              " Close All ",
              " Save               (Ctrl+S) ",
              " Save All ",
              " [ ] Add to Favorites ",
              " Exit               (Ctrl+Q) ",
          },
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
              " Search in Files...",
              " Text Processors...",
          },
          {
              " Toggle Line Numbers ",
              " Toggle File Explorer (Ctrl+B) ",
              " Smart Word Wrap [ ] ",
              " Syntax Highlighting [X] ",
              " Auto Pairing [X] ",
              " Smart Auto-Indent [X] ",
              " Tab Size: 4 spaces ",
              " Convert Tabs to Spaces ",
              " Theme... ",
          },
          {
              " TTS                 (Alt+H) ",
              " AI Actions          (Alt+J) ",
              " Assistant Settings  (Alt+S) ",
          },
          {
              " Git... ",
              " Git Settings... ",
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

    RebuildDropdownComponents();
    menu_container_ = ftxui::Container::Vertical({
        top_menu_,
        dropdown_menu_,
    });
    Add(menu_container_);
}

void MenuBarComponent::RebuildDropdownComponents() {
    dropdown_buttons_.clear();
    dropdown_buttons_.reserve(current_dropdown_entries_.size());

    for (size_t index = 0; index < current_dropdown_entries_.size(); ++index) {
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
                return item |
                    ftxui::bgcolor(theme.menu_foreground) |
                    ftxui::color(theme.menu_background);
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
    if (active_dropdown_ >= 0) {
        if (dropdown_menu_->OnEvent(event)) {
            return true;
        }

        if (top_menu_->OnEvent(event)) {
            if (mouse.button == ftxui::Mouse::Left &&
                mouse.motion == ftxui::Mouse::Released) {
                OpenDropdown(selected_menu_item_);
            }
            return true;
        }

        if (mouse.button == ftxui::Mouse::Left &&
            mouse.motion == ftxui::Mouse::Pressed) {
            const bool in_top_menu =
                !menu_boxes_.empty() && menu_boxes_[0].Contain(mouse.x, mouse.y);
            const bool in_dropdown =
                active_dropdown_ < static_cast<int>(dropdown_boxes_.size()) &&
                dropdown_boxes_[active_dropdown_].Contain(mouse.x, mouse.y);
            if (!in_top_menu && !in_dropdown) {
                CloseDropdown();
            }
        }
        return true;
    }

    if (!top_menu_->OnEvent(event)) {
        return false;
    }
    if (mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Released &&
        active_dropdown_ < 0) {
        OpenDropdown(selected_menu_item_);
    }
    return true;
}
    
    
void MenuBarComponent::SetFileFavoriteLabel(bool is_favorite) {
    if (dropdown_entries_.empty() || dropdown_entries_[0].size() <= 11) {
        return;
    }

    dropdown_entries_[0][11] = is_favorite
        ? " [X] Remove from Favorites "
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
    const std::vector<std::string>& next_entries = dropdown_entries_[menu_index];
    if (current_dropdown_entries_ == next_entries) {
        return;
    }
    current_dropdown_entries_ = next_entries;
    if (selected_dropdown_item_ >= static_cast<int>(current_dropdown_entries_.size())) {
        selected_dropdown_item_ = 0;
    }
    if (dropdown_menu_) {
        RebuildDropdownComponents();
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

} // namespace textlt
