#include "theme_dialog.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {

// --- ThemeSelectionContent Implementation ---

ThemeSelectionContent::ThemeSelectionContent(const Theme* active_theme,
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
        // Use the current active_theme_ for rendering
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
}

void ThemeSelectionContent::SetThemes(const std::vector<Theme>& themes, const std::string& active_name) {
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
}

void ThemeSelectionContent::SelectCurrentTheme() {
    if (selected_theme_ < 0 || selected_theme_ >= static_cast<int>(theme_names_.size())) {
        return;
    }
    if (on_select_) {
        on_select_(theme_names_[selected_theme_]);
    }
    // Note: ThemeSelectionContent itself does not close. Its parent ModalWindow handles closing.
}

ftxui::Element ThemeSelectionContent::Render() {
    using namespace ftxui;
    const Theme& theme = active_theme_ ? *active_theme_ : FallbackTheme();

    return menu_->Render() |
        bgcolor(theme.modal_input_bg) |
        color(theme.modal_input_fg);
}

void ThemeSelectionContent::TakeFocus() {
    menu_->TakeFocus();
}

// --- ThemeDialog Implementation ---

ThemeDialog::ThemeDialog(
    const Theme* active_theme,
    ThemeCallback on_preview,
    ThemeCallback on_select,
    CloseCallback on_close)
    : current_active_theme_(active_theme),
      on_close_(std::move(on_close)) {
    
    content_impl_ = std::make_shared<ThemeSelectionContent>(current_active_theme_, on_preview, [this, on_select](const std::string& theme_name) {
        on_select(theme_name);
        RequestClose();
    });
    modal_window_ = std::make_shared<ModalWindow>(
        content_impl_, current_active_theme_, [this] { RequestClose(); });
    modal_window_->SetFooterButtons({
        {"Select", [this] {
            if (content_impl_) {
                content_impl_->SelectCurrentTheme();
            }
        }},
        {"Close", [this] { RequestClose(); }},
    });
}

ftxui::Component ThemeDialog::View() const {
    return modal_window_;
}

void ThemeDialog::Open(const std::vector<Theme>& themes, const std::string& active_name) {
    open_ = true;
    content_impl_->SetThemes(themes, active_name);
    // Ensure the theme in content_impl_ and modal_window_ is the most current one.
    // If the active_theme_ passed to TextltApp::theme_dialog_ changed, it needs to be set.
    // TextltApp should call SetTheme on ThemeDialog directly.
    content_impl_->SetTheme(current_active_theme_);
    modal_window_->SetTheme(current_active_theme_);
    content_impl_->TakeFocus();
}

void ThemeDialog::Close() {
    open_ = false;
}

void ThemeDialog::RequestClose() {
    if (!open_) {
        return;
    }
    open_ = false;
    if (on_close_) {
        on_close_();
    }
}

bool ThemeDialog::IsOpen() const {
    return open_;
}

void ThemeDialog::TakeFocus() {
    if (content_impl_) {
        content_impl_->TakeFocus();
    }
}

void ThemeDialog::SetTheme(const Theme* new_theme) {
    current_active_theme_ = new_theme;
    if (content_impl_) {
        content_impl_->SetTheme(new_theme);
    }
    if (modal_window_) {
        modal_window_->SetTheme(new_theme);
    }
}

} // namespace textlt
