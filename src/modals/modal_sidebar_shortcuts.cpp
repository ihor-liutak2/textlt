#include "modal_sidebar_shortcuts.hpp"

#include <cctype>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "keyboard_shortcuts.hpp"

namespace textlt {

namespace {

std::string EntryLabel(char key, const std::string& label) {
    return std::string(1, key) + "  " + label;
}

} // namespace

SidebarShortcutModalContent::SidebarShortcutModalContent(
    const Theme* theme,
    RunCallback run_callback,
    CloseCallback close_callback)
    : theme_(theme),
      run_callback_(std::move(run_callback)),
      close_callback_(std::move(close_callback)),
      entries_({
          {'B', "Toggle sidebar", "sidebar.toggle_file_explorer"},
          {'O', "Opened files (Sidebar)", "sidebar.show_opened_files"},
          {'F', "Favorites (Sidebar)", "sidebar.show_favorites"},
          {'P', "Project (Sidebar)", "sidebar.show_project"},
          {'M', "File manager", "file.files"},
          {'S', "Search in files", "search.files"},
          {'T', "Text processors", "text_processors.open"},
          {'G', "Git", "git.open"},
      }) {
    for (const Entry& entry : entries_) {
        entry_labels_.push_back(EntryLabel(entry.key, entry.label));
    }

    ftxui::MenuOption option = ftxui::MenuOption::Vertical();
    option.on_enter = [this] { TriggerEntry(static_cast<size_t>(selected_entry_)); };
    option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        ftxui::Element item = ftxui::text(state.label);
        if (state.focused || state.active) {
            return item |
                ftxui::bgcolor(theme.modal_selected_item_bg) |
                ftxui::color(theme.modal_selected_item_fg);
        }
        return item | ftxui::color(theme.modal_text_color);
    };

    menu_ = ftxui::Menu(&entry_labels_, &selected_entry_, option);
    container_ = ftxui::CatchEvent(menu_, [this](ftxui::Event event) {
        return HandleEvent(std::move(event));
    });
}

void SidebarShortcutModalContent::Open() {
    selected_entry_ = 0;
    if (container_) {
        container_->TakeFocus();
    }
}

void SidebarShortcutModalContent::Close() {}

bool SidebarShortcutModalContent::IsEntryKey(ftxui::Event event, char key) const {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(key)));

    return MatchesPlainShortcutKey(event, lower) ||
        MatchesShortcut(event, ShortcutModifier::Ctrl, lower) ||
        event.input() == "Ctrl+" + std::string(1, upper) ||
        event == ftxui::Event::Special("Ctrl+" + std::string(1, upper));
}

void SidebarShortcutModalContent::TriggerEntry(size_t index) {
    if (index >= entries_.size()) {
        return;
    }

    if (close_callback_) {
        close_callback_();
    }
    if (run_callback_) {
        run_callback_(entries_[index].command_id);
    }
}

bool SidebarShortcutModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        if (close_callback_) {
            close_callback_();
        }
        return true;
    }

    for (size_t index = 0; index < entries_.size(); ++index) {
        if (IsEntryKey(event, entries_[index].key)) {
            TriggerEntry(index);
            return true;
        }
    }

    return false;
}

ftxui::Element SidebarShortcutModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    return vbox({
        menu_ ? menu_->Render() : emptyElement(),
    }) |
        color(theme.modal_text_color);
}

SidebarShortcutModal::SidebarShortcutModal(
    const Theme* theme,
    SidebarShortcutModalContent::RunCallback run_callback,
    SidebarShortcutModalContent::CloseCallback close_callback)
    : theme_(theme),
      run_callback_(std::move(run_callback)),
      request_close_callback_(std::move(close_callback)) {
    content_ = std::make_shared<SidebarShortcutModalContent>(
        theme_,
        run_callback_,
        request_close_callback_);
    modal_ = std::make_shared<ModalWindow>(content_, theme_, request_close_callback_);
}

ftxui::Component SidebarShortcutModal::View() const {
    return modal_;
}

void SidebarShortcutModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
        modal_->SetHeaderCloseVisible(false);
        modal_->SetFooterVisible(false);
        modal_->SetModalSize(34, 12);
    }
}

void SidebarShortcutModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool SidebarShortcutModal::IsOpen() const {
    return open_;
}

bool SidebarShortcutModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }
    return modal_->OnEvent(std::move(event));
}

} // namespace textlt
