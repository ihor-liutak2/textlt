#include "theme_dialog.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

ThemeDialog::ThemeDialog(const Theme* active_theme,
                         ThemeCallback on_preview,
                         ThemeCallback on_select)
    : active_theme_(active_theme),
      on_preview_(std::move(on_preview)),
      on_select_(std::move(on_select)) {
    
    ftxui::MenuOption option = ftxui::MenuOption::Vertical();
    
    // Save and close the dialog explicitly when Enter key is pressed
    option.on_enter = [this] { SelectCurrentTheme(); };
    
    // Dynamically apply the selected theme option in real-time as the index shifts
    option.on_change = [this] { 
        if (selected_theme_ >= 0 && selected_theme_ < static_cast<int>(theme_names_.size())) {
            if (on_preview_) {
                on_preview_(theme_names_[selected_theme_]);
            }
        }
    };
    
    // Customize rendering for each theme item in the list
    option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = active_theme_ ? *active_theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text((state.active ? "> " : "  ") + state.label);
        
        // Highlight active or focused selection row
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        // Standard unselected text color inside modal
        return item | ftxui::color(theme.modal_text_color);
    };
    
    menu_ = ftxui::Menu(&theme_names_, &selected_theme_, option);
    
    // Safe renderer assembly without conflicting mouse capture events
    renderer_ = ftxui::Renderer(menu_, [this] { return Render(); });
}

ftxui::Component ThemeDialog::View() const {
    return renderer_;
}

void ThemeDialog::Open(const std::vector<Theme>& themes, const std::string& active_name) {
    theme_names_.clear();
    for (const Theme& theme : themes) {
        theme_names_.push_back(theme.name);
    }
    if (theme_names_.empty()) {
        theme_names_.push_back("Blueprint");
    }

    auto iter = std::find(theme_names_.begin(), theme_names_.end(), active_name);
    selected_theme_ = iter == theme_names_.end()
        ? 0
        : static_cast<int>(std::distance(theme_names_.begin(), iter));
    open_ = true;
    TakeFocus();
}

void ThemeDialog::Close() {
    open_ = false;
}

bool ThemeDialog::IsOpen() const {
    return open_;
}

void ThemeDialog::TakeFocus() {
    menu_->TakeFocus();
}

void ThemeDialog::SelectCurrentTheme() {
    if (selected_theme_ < 0 || selected_theme_ >= static_cast<int>(theme_names_.size())) {
        return;
    }
    if (on_select_) {
        on_select_(theme_names_[selected_theme_]);
    }
    Close();
}

ftxui::Element ThemeDialog::Render() {
    using namespace ftxui;
    const Theme& theme = active_theme_ ? *active_theme_ : FallbackTheme();

    Element content = vbox({
        // Isolated list view container to prevent text color bleed-through.
        vbox({
            menu_->Render()
        }) |
            bgcolor(theme.modal_input_bg) |
            color(theme.modal_input_fg) |
            frame |
            size(WIDTH, EQUAL, 42) |
            size(HEIGHT, LESS_THAN, 14),

        separator() | color(theme.modal_border),
        
        // Footer hint text block
        text(" Selection saves immediately. Escape closes. ") | dim | color(theme.modal_text_color),
    }) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color);

    // Keep modal paint bounded to the window instead of the full overlay layer.
    return window(
        text(" Select Theme ") | bold | color(theme.modal_accent),
        content) |
        bgcolor(theme.modal_background) |
        color(theme.modal_text_color) |
        size(WIDTH, EQUAL, 46) |
        clear_under;
}

} // namespace textlt
