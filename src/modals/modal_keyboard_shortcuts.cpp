#include "modal_keyboard_shortcuts.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

#include "ui_button.hpp"

namespace textlt {
namespace {

ftxui::Element TextButtonElement(const std::string& label,
                               const Theme& theme,
                               bool active,
                               bool selected = false) {
    ButtonRole role = ButtonRoleFromLabel(label);
    if (label == "Menu shortcuts" || label == "Text shortcuts") {
        role = ButtonRole::Tab;
    }
    ButtonSpec spec = ButtonSpecFromLabel(label, role, ButtonVariant::AccentEdges, ButtonSize::Compact);
    spec.selected = selected;
    return RenderButton(theme, spec, active);
}

std::string DisplayShortcut(const std::string& shortcut) {
    return shortcut.empty() ? "-" : shortcut;
}

} // namespace

KeyboardShortcutsModalContent::KeyboardShortcutsModalContent(
    const Theme* theme,
    ShortcutRegistry* shortcut_registry,
    const AppCommandRegistry* command_registry,
    SaveCallback save_callback,
    CloseCallback close_callback)
    : theme_(theme),
      shortcut_registry_(shortcut_registry),
      command_registry_(command_registry),
      save_callback_(std::move(save_callback)),
      close_callback_(std::move(close_callback)) {
    menu_tab_button_ = MakeTextButton("Menu shortcuts", [this] { SetTab(0); });
    text_tab_button_ = MakeTextButton("Text shortcuts", [this] { SetTab(1); });

    modifier_labels_.clear();
    for (ShortcutKeyModifier modifier : ShortcutModifierChoices()) {
        modifier_labels_.push_back(ShortcutModifierName(modifier));
    }
    modifier_menu_ = ftxui::Menu(&modifier_labels_, &selected_modifier_);
    key_menu_ = ftxui::Menu(&key_labels_, &selected_key_);
    action_menu_ = ftxui::Menu(&action_labels_, &selected_action_);

    apply_button_ = MakeTextButton("Apply", [this] { ApplySelectedShortcut(); });
    reset_button_ = MakeTextButton("Reset", [this] { ResetSelectedShortcut(); });
    reset_all_button_ = MakeTextButton("Reset All", [this] { ResetAllShortcuts(); });
    close_button_ = MakeTextButton("Close", [this] {
        if (close_callback_) {
            close_callback_();
        }
    });

    picker_container_ = ftxui::Container::Horizontal({modifier_menu_, key_menu_});
    button_container_ = ftxui::Container::Horizontal({apply_button_, reset_button_, reset_all_button_, close_button_});
    container_ = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({menu_tab_button_, text_tab_button_}),
        action_menu_,
        picker_container_,
        button_container_,
    });
    container_ = ftxui::CatchEvent(container_, [this](ftxui::Event event) {
        return HandleEvent(event);
    });

    RebuildActionList();
}

ftxui::Component KeyboardShortcutsModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        const bool selected =
            (state.label == "Menu shortcuts" && tab_index_ == 0) ||
            (state.label == "Text shortcuts" && tab_index_ == 1);
        return TextButtonElement(state.label, theme, state.focused || state.active, selected);
    };
    return ftxui::Button(std::move(option));
}

void KeyboardShortcutsModalContent::Open() {
    status_ = "Choose a command, modifier and key. Terminal-reserved shortcuts are hidden.";
    status_is_error_ = false;
    RebuildActionList();
    SyncSelectionFromBinding();
}

void KeyboardShortcutsModalContent::Close() {}

ShortcutContext KeyboardShortcutsModalContent::CurrentContext() const {
    return tab_index_ == 0 ? ShortcutContext::Menu : ShortcutContext::Text;
}

std::vector<ShortcutBindingView> KeyboardShortcutsModalContent::CurrentBindings() const {
    if (!shortcut_registry_) {
        return {};
    }
    return shortcut_registry_->Bindings(CurrentContext());
}

void KeyboardShortcutsModalContent::RebuildActionList() {
    bindings_ = CurrentBindings();
    action_labels_.clear();
    action_labels_.reserve(bindings_.size());
    for (const ShortcutBindingView& binding : bindings_) {
        std::string label = binding.definition.category + " / " + binding.definition.title;
        label += "  " + DisplayShortcut(binding.effective_shortcut);
        if (binding.custom) {
            label += "  custom";
        }
        action_labels_.push_back(std::move(label));
    }
    if (selected_action_ >= static_cast<int>(bindings_.size())) {
        selected_action_ = bindings_.empty() ? 0 : static_cast<int>(bindings_.size()) - 1;
    }
    if (selected_action_ < 0) {
        selected_action_ = 0;
    }
    RebuildKeyList();
}

void KeyboardShortcutsModalContent::RebuildKeyList() {
    const auto modifiers = ShortcutModifierChoices();
    if (selected_modifier_ < 0 || selected_modifier_ >= static_cast<int>(modifiers.size())) {
        selected_modifier_ = 0;
    }
    key_labels_.clear();
    const ShortcutKeyModifier modifier = modifiers[selected_modifier_];
    const auto all_keys = ShortcutKeyChoices(modifier);
    const auto selected_binding = SelectedBinding();
    const std::string selected_action_id =
        selected_binding ? selected_binding->definition.action_id : std::string();

    for (std::string key : all_keys) {
        if (key == "/") {
            key = "Slash";
        }
        if (shortcut_registry_ && !selected_action_id.empty()) {
            const std::string shortcut = ShortcutModifierName(modifier) + "+" +
                (key == "Slash" ? std::string("/") : key);
            const ShortcutConflict conflict =
                shortcut_registry_->FindConflict(CurrentContext(), selected_action_id, shortcut);
            if (conflict.exists) {
                continue;
            }
        }
        key_labels_.push_back(std::move(key));
    }
    if (selected_key_ < 0 || selected_key_ >= static_cast<int>(key_labels_.size())) {
        selected_key_ = 0;
    }
}

void KeyboardShortcutsModalContent::SyncSelectionFromBinding() {
    const auto binding = SelectedBinding();
    if (!binding) {
        return;
    }
    const auto parsed = ParseShortcutKey(binding->effective_shortcut);
    if (!parsed) {
        return;
    }

    const auto modifiers = ShortcutModifierChoices();
    const auto modifier_it = std::find(modifiers.begin(), modifiers.end(), parsed->modifier);
    if (modifier_it != modifiers.end()) {
        selected_modifier_ = static_cast<int>(std::distance(modifiers.begin(), modifier_it));
    }
    RebuildKeyList();

    const std::string wanted_key = parsed->key == "/" ? "Slash" : parsed->key;
    const auto key_it = std::find(key_labels_.begin(), key_labels_.end(), wanted_key);
    if (key_it != key_labels_.end()) {
        selected_key_ = static_cast<int>(std::distance(key_labels_.begin(), key_it));
    }
}

std::optional<ShortcutBindingView> KeyboardShortcutsModalContent::SelectedBinding() const {
    if (selected_action_ < 0 || selected_action_ >= static_cast<int>(bindings_.size())) {
        return std::nullopt;
    }
    return bindings_[selected_action_];
}

std::string KeyboardShortcutsModalContent::SelectedShortcutString() const {
    const auto modifiers = ShortcutModifierChoices();
    if (selected_modifier_ < 0 || selected_modifier_ >= static_cast<int>(modifiers.size()) ||
        selected_key_ < 0 || selected_key_ >= static_cast<int>(key_labels_.size())) {
        return "";
    }
    std::string key = key_labels_[selected_key_];
    if (key == "Slash") {
        key = "/";
    }
    return ShortcutModifierName(modifiers[selected_modifier_]) + "+" + key;
}

bool KeyboardShortcutsModalContent::ApplySelectedShortcut() {
    const auto binding = SelectedBinding();
    if (!binding || !shortcut_registry_) {
        status_ = "No command selected.";
        status_is_error_ = true;
        return false;
    }

    std::string error;
    const std::string shortcut = SelectedShortcutString();
    if (!shortcut_registry_->SetOverride(CurrentContext(), binding->definition.action_id, shortcut, error)) {
        status_ = error;
        status_is_error_ = true;
        return false;
    }
    if (save_callback_ && !save_callback_(error)) {
        status_ = error;
        status_is_error_ = true;
        return false;
    }
    status_ = "Assigned " + shortcut + " to " + binding->definition.title + ".";
    status_is_error_ = false;
    RebuildActionList();
    return true;
}

void KeyboardShortcutsModalContent::ResetSelectedShortcut() {
    const auto binding = SelectedBinding();
    if (!binding || !shortcut_registry_) {
        return;
    }
    shortcut_registry_->ClearOverride(CurrentContext(), binding->definition.action_id);
    std::string error;
    if (save_callback_ && !save_callback_(error)) {
        status_ = error;
        status_is_error_ = true;
        return;
    }
    status_ = "Reset " + binding->definition.title + " to default.";
    status_is_error_ = false;
    RebuildActionList();
    SyncSelectionFromBinding();
}

void KeyboardShortcutsModalContent::ResetAllShortcuts() {
    if (!shortcut_registry_) {
        return;
    }
    shortcut_registry_->ClearAllOverrides();
    std::string error;
    if (save_callback_ && !save_callback_(error)) {
        status_ = error;
        status_is_error_ = true;
        return;
    }
    status_ = "Reset all shortcuts to defaults.";
    status_is_error_ = false;
    RebuildActionList();
    SyncSelectionFromBinding();
}

void KeyboardShortcutsModalContent::SetTab(int tab_index) {
    tab_index_ = std::clamp(tab_index, 0, 1);
    selected_action_ = 0;
    action_top_row_ = 0;
    RebuildActionList();
    SyncSelectionFromBinding();
}

void KeyboardShortcutsModalContent::MoveSelection(int delta) {
    if (bindings_.empty()) {
        selected_action_ = 0;
        return;
    }
    selected_action_ = std::clamp(selected_action_ + delta, 0, static_cast<int>(bindings_.size()) - 1);
    EnsureSelectionVisible();
    SyncSelectionFromBinding();
}

void KeyboardShortcutsModalContent::EnsureSelectionVisible() {
    if (selected_action_ < action_top_row_) {
        action_top_row_ = selected_action_;
    }
    if (selected_action_ >= action_top_row_ + kVisibleActionRows) {
        action_top_row_ = selected_action_ - kVisibleActionRows + 1;
    }
}

bool KeyboardShortcutsModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        if (close_callback_) {
            close_callback_();
        }
        return true;
    }
    if (event == ftxui::Event::ArrowDown && action_menu_->Focused()) {
        MoveSelection(1);
        return false;
    }
    if (event == ftxui::Event::ArrowUp && action_menu_->Focused()) {
        MoveSelection(-1);
        return false;
    }
    if (event == ftxui::Event::Tab) {
        SetTab(tab_index_ == 0 ? 1 : 0);
        return true;
    }
    if (event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
        const ftxui::Mouse& mouse = event.mouse();
        if (menu_tab_box_.Contain(mouse.x, mouse.y)) {
            SetTab(0);
            return true;
        }
        if (text_tab_box_.Contain(mouse.x, mouse.y)) {
            SetTab(1);
            return true;
        }
        for (int index = action_top_row_;
             index < static_cast<int>(action_row_boxes_.size());
             ++index) {
            if (action_row_boxes_[index].Contain(mouse.x, mouse.y)) {
                selected_action_ = index;
                EnsureSelectionVisible();
                // A command change discards the un-applied picker state and
                // presents the clicked command's current effective binding.
                SyncSelectionFromBinding();
                return true;
            }
        }
    }
    return false;
}

ftxui::Element KeyboardShortcutsModalContent::RenderTitle() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Keyboard Shortcuts ") | bold | color(theme.modal_foreground),
        filler(),
        text("shortcuts.json overrides only") | color(theme.modal_text_color),
    });
}

ftxui::Element KeyboardShortcutsModalContent::RenderTabs() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        TextButtonElement("Menu shortcuts", theme, false, tab_index_ == 0) |
            reflect(menu_tab_box_),
        text(" "),
        TextButtonElement("Text shortcuts", theme, false, tab_index_ == 1) |
            reflect(text_tab_box_),
        filler(),
        text("Tab switches sections") | color(theme.modal_text_color),
    });
}

ftxui::Element KeyboardShortcutsModalContent::RenderActionList() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    action_row_boxes_.resize(bindings_.size());
    const int end = std::min(static_cast<int>(bindings_.size()), action_top_row_ + kVisibleActionRows);
    for (int index = action_top_row_; index < end; ++index) {
        const bool selected = index == selected_action_;
        const ShortcutBindingView& binding = bindings_[index];
        Element row = hbox({
            text(binding.definition.category) | size(WIDTH, EQUAL, 14),
            text(binding.definition.title) | size(WIDTH, EQUAL, 34),
            text(DisplayShortcut(binding.effective_shortcut)) | size(WIDTH, EQUAL, 18),
            text(binding.custom ? "custom" : "default") | size(WIDTH, EQUAL, 9),
        }) | reflect(action_row_boxes_[index]);
        if (selected) {
            row = row | bgcolor(theme.menu_foreground) | color(theme.menu_background);
        } else {
            row = row | color(theme.modal_text_color);
        }
        rows.push_back(row);
    }
    while (rows.size() < static_cast<size_t>(kVisibleActionRows)) {
        rows.push_back(text(""));
    }
    return vbox({
        hbox({
            text("Category") | bold | size(WIDTH, EQUAL, 14),
            text("Command / action") | bold | size(WIDTH, EQUAL, 34),
            text("Shortcut") | bold | size(WIDTH, EQUAL, 18),
            text("State") | bold | size(WIDTH, EQUAL, 9),
        }) | color(theme.modal_accent),
        separator(),
        vbox(std::move(rows)) | size(HEIGHT, EQUAL, kVisibleActionRows),
    }) | border | size(WIDTH, EQUAL, 82);
}

ftxui::Element KeyboardShortcutsModalContent::RenderPicker() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const std::string selected_shortcut = SelectedShortcutString();
    return vbox({
        text("Assign selected action") | bold | color(theme.modal_accent),
        separator(),
        hbox({
            vbox({
                text("Modifier") | bold,
                modifier_menu_->Render() | frame | size(HEIGHT, EQUAL, 8),
            }) | size(WIDTH, EQUAL, 18),
            separator(),
            vbox({
                text("Key") | bold,
                key_menu_->Render() | frame | size(HEIGHT, EQUAL, 8),
            }) | size(WIDTH, EQUAL, 18),
        }),
        separator(),
        paragraph("New: " + DisplayShortcut(selected_shortcut)) |
            color(theme.modal_accent) |
            flex,
        paragraph("Only unassigned keys are shown for the selected modifier. Reserved terminal shortcuts are not listed.") |
            color(theme.modal_text_color) |
            flex,
    }) | border | size(WIDTH, EQUAL, 42);
}

ftxui::Element KeyboardShortcutsModalContent::RenderHelpLine() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return text(status_) | color(status_is_error_ ? ftxui::Color::Red : theme.modal_text_color);
}

ftxui::Element KeyboardShortcutsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    RebuildKeyList();
    return vbox({
        RenderTabs(),
        separator(),
        hbox({
            RenderActionList(),
            separator(),
            RenderPicker(),
        }),
        separator(),
        hbox({
            apply_button_->Render(),
            text(" "),
            reset_button_->Render(),
            text(" "),
            reset_all_button_->Render(),
            filler(),
            close_button_->Render(),
        }),
        RenderHelpLine(),
    }) | color(theme.modal_text_color);
}

KeyboardShortcutsModal::KeyboardShortcutsModal(
    const Theme* theme,
    ShortcutRegistry* shortcut_registry,
    const AppCommandRegistry* command_registry,
    KeyboardShortcutsModalContent::SaveCallback save_callback)
    : theme_(theme),
      shortcut_registry_(shortcut_registry),
      command_registry_(command_registry),
      save_callback_(std::move(save_callback)) {
    content_ = std::make_shared<KeyboardShortcutsModalContent>(
        theme_,
        shortcut_registry_,
        command_registry_,
        save_callback_,
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
}

ftxui::Component KeyboardShortcutsModal::View() const {
    return modal_;
}

void KeyboardShortcutsModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
        modal_->SetModalSize(108, 32);
    }
}

void KeyboardShortcutsModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool KeyboardShortcutsModal::IsOpen() const {
    return open_;
}

bool KeyboardShortcutsModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }
    if (event == ftxui::Event::Escape) {
        Close();
        return true;
    }
    return modal_->OnEvent(event);
}

} // namespace textlt
