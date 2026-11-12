#include "modals/modal_theme_colors.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "ui_button.hpp"

namespace textlt {
namespace {

ftxui::Component MakeTabButton(
    const Theme** theme,
    std::string label,
    std::function<void()> callback,
    std::function<bool()> selected) {
    ButtonSpec spec = ButtonSpecFromLabel(
        std::move(label), ButtonRole::Tab, ButtonVariant::AccentEdges, ButtonSize::Compact);
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(spec);
    option.on_click = std::move(callback);
    option.transform = [theme, spec, selected = std::move(selected)](
                           const ftxui::EntryState& state) mutable {
        const Theme& resolved = theme && *theme ? **theme : FallbackTheme();
        spec.selected = selected && selected();
        return RenderModalFlatButton(resolved, spec, state.focused || state.active);
    };
    return ftxui::Button(option);
}

} // namespace

ThemeColorsModalContent::ThemeColorsModalContent(
    const Theme* theme,
    ApplyCallback apply_callback,
    ResetCallback reset_callback,
    CloseCallback close_callback)
    : theme_(theme),
      apply_callback_(std::move(apply_callback)),
      reset_callback_(std::move(reset_callback)),
      close_callback_(std::move(close_callback)) {
    BuildFields();
    BuildComponents();
    LoadValuesFromTheme();

    apply_button_ = MakePrimaryButton(
        theme_, "Apply", [this] { Apply(); });
    reset_button_ = MakeWarningButton(
        theme_, "Reset Default", [this] { ResetToDefault(); });
    close_button_ = MakeCancelButton(
        theme_, [this] {
            if (close_callback_) {
                close_callback_();
            }
        }, "Close");
}

void ThemeColorsModalContent::BuildFields() {
    fields_ = {
        {"background", "Background", &Theme::background, 0},
        {"foreground", "Foreground", &Theme::foreground, 0},
        {"header", "Header", &Theme::header, 0},
        {"menu_background", "Menu background", &Theme::menu_background, 0},
        {"menu_foreground", "Menu foreground", &Theme::menu_foreground, 0},
        {"status", "Status", &Theme::status, 0},
        {"gutter", "Gutter / borders", &Theme::gutter, 0},
        {"cursor", "Cursor", &Theme::cursor, 0},
        {"selection_bg", "Selection background", &Theme::selection_bg, 0},
        {"selection_fg", "Selection foreground", &Theme::selection_fg, 0},

        {"editor_text", "Editor text", &Theme::editor_text, 1},
        {"syntax_keyword", "Syntax keyword", &Theme::syntax_keyword, 1},
        {"syntax_type", "Syntax type", &Theme::syntax_type, 1},
        {"syntax_string", "Syntax string", &Theme::syntax_string, 1},
        {"syntax_number", "Syntax number", &Theme::syntax_number, 1},
        {"syntax_comment", "Syntax comment", &Theme::syntax_comment, 1},
        {"match_bg", "Search match", &Theme::match_bg, 1},
        {"active_match_bg", "Active search match", &Theme::active_match_bg, 1},

        {"modal_background", "Modal background", &Theme::modal_background, 2},
        {"modal_foreground", "Modal foreground", &Theme::modal_foreground, 2},
        {"modal_border", "Modal border", &Theme::modal_border, 2},
        {"modal_accent", "Modal accent", &Theme::modal_accent, 2},
        {"modal_text_color", "Modal text", &Theme::modal_text_color, 2},
        {"modal_input_bg", "Input background", &Theme::modal_input_bg, 2},
        {"modal_input_fg", "Input foreground", &Theme::modal_input_fg, 2},
        {"modal_selected_item_bg", "Selected item background", &Theme::modal_selected_item_bg, 2},
        {"modal_selected_item_fg", "Selected item foreground", &Theme::modal_selected_item_fg, 2},

        {"button_default", "Default button", &Theme::button_default, 3},
        {"button_primary", "Primary button", &Theme::button_primary, 3},
        {"button_secondary", "Secondary button", &Theme::button_secondary, 3},
        {"button_success", "Success button", &Theme::button_success, 3},
        {"button_warning", "Warning button", &Theme::button_warning, 3},
        {"button_danger", "Danger button", &Theme::button_danger, 3},
        {"button_cancel", "Cancel button", &Theme::button_cancel, 3},
        {"button_utility", "Utility button", &Theme::button_utility, 3},
        {"button_navigation", "Navigation button", &Theme::button_navigation, 3},
        {"button_tab", "Tab button", &Theme::button_tab, 3},
        {"button_toggle", "Toggle button", &Theme::button_toggle, 3},
        {"button_media", "Media button", &Theme::button_media, 3},
        {"button_text", "Button text", &Theme::button_text, 3},
        {"button_muted_text", "Muted button text", &Theme::button_muted_text, 3},
        {"button_focused_bg", "Focused background", &Theme::button_focused_bg, 3},
        {"button_focused_fg", "Focused foreground", &Theme::button_focused_fg, 3},
        {"button_selected_bg", "Selected background", &Theme::button_selected_bg, 3},
        {"button_selected_fg", "Selected foreground", &Theme::button_selected_fg, 3},
        {"button_disabled_fg", "Disabled foreground", &Theme::button_disabled_fg, 3},
        {"button_shadow", "Button shadow", &Theme::button_shadow, 3},
        {"button_bracket", "Button brackets", &Theme::button_bracket, 3},
    };
}

void ThemeColorsModalContent::BuildComponents() {
    inputs_.reserve(fields_.size());
    for (ColorField& field : fields_) {
        ftxui::InputOption option;
        option.multiline = false;
        option.cursor_position = &field.cursor;
        option.transform = [this](ftxui::InputState state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            return theme.InputTransform(std::move(state));
        };
        inputs_.push_back(ftxui::Input(&field.value, "#RRGGBB", option));
    }

    for (int group = 0; group < 4; ++group) {
        std::vector<ftxui::Component> group_inputs;
        for (size_t index = 0; index < fields_.size(); ++index) {
            if (fields_[index].group == group) {
                group_inputs.push_back(inputs_[index]);
            }
        }
        group_components_.push_back(ftxui::Container::Vertical(std::move(group_inputs)));
    }

    interface_tab_ = MakeTabButton(
        &theme_, "Interface", [this] { SelectGroup(0); }, [this] { return selected_group_ == 0; });
    editor_tab_ = MakeTabButton(
        &theme_, "Editor & Syntax", [this] { SelectGroup(1); }, [this] { return selected_group_ == 1; });
    modal_tab_ = MakeTabButton(
        &theme_, "Modal", [this] { SelectGroup(2); }, [this] { return selected_group_ == 2; });
    buttons_tab_ = MakeTabButton(
        &theme_, "Buttons", [this] { SelectGroup(3); }, [this] { return selected_group_ == 3; });
    tabs_ = ftxui::Container::Horizontal({interface_tab_, editor_tab_, modal_tab_, buttons_tab_});
    groups_ = ftxui::Container::Tab(group_components_, &selected_group_);
    container_ = ftxui::Container::Vertical({tabs_, groups_});
}

void ThemeColorsModalContent::LoadValuesFromTheme() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    for (ColorField& field : fields_) {
        field.value = ThemeColorToString(theme.*(field.member));
        field.cursor = static_cast<int>(field.value.size());
    }
}

void ThemeColorsModalContent::SelectGroup(int group) {
    selected_group_ = std::clamp(group, 0, 3);
    if (selected_group_ < static_cast<int>(group_components_.size())) {
        group_components_[selected_group_]->TakeFocus();
    }
}

void ThemeColorsModalContent::Open() {
    selected_group_ = 0;
    LoadValuesFromTheme();
    status_ = "Editing " + (theme_ ? theme_->name : std::string("active theme")) +
        ". Use a named color or #RRGGBB.";
    status_is_error_ = false;
    if (!group_components_.empty()) {
        group_components_.front()->TakeFocus();
    }
}

bool ThemeColorsModalContent::Apply() {
    std::map<std::string, std::string> overrides;
    for (ColorField& field : fields_) {
        ftxui::Color parsed;
        if (!ParseThemeColor(field.value, parsed)) {
            status_ = "Invalid " + field.label + ": use a named color or #RRGGBB.";
            status_is_error_ = true;
            return false;
        }
        field.value = ThemeColorToString(parsed);
        field.cursor = static_cast<int>(field.value.size());
        overrides[field.key] = field.value;
    }

    std::string error;
    if (!apply_callback_ || !apply_callback_(overrides, error)) {
        status_ = error.empty() ? "Could not save theme colors." : error;
        status_is_error_ = true;
        return false;
    }
    status_ = "Theme colors applied and saved.";
    status_is_error_ = false;
    return true;
}

void ThemeColorsModalContent::ResetToDefault() {
    std::string error;
    if (reset_callback_ && !reset_callback_(error)) {
        status_ = error.empty() ? "Could not reset theme colors." : error;
        status_is_error_ = true;
        return;
    }
    LoadValuesFromTheme();
    status_ = "Restored the default colors for this theme.";
    status_is_error_ = false;
}

ftxui::Element ThemeColorsModalContent::RenderTabs() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        interface_tab_->Render(),
        text(" "),
        editor_tab_->Render(),
        text(" "),
        modal_tab_->Render(),
        text(" "),
        buttons_tab_->Render(),
        filler(),
        text(theme.name) | bold | color(theme.modal_accent),
    });
}

ftxui::Element ThemeColorsModalContent::RenderGroup() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    Elements rows;
    for (size_t index = 0; index < fields_.size(); ++index) {
        const ColorField& field = fields_[index];
        if (field.group != selected_group_) {
            continue;
        }
        ftxui::Color preview = theme.modal_input_bg;
        const bool valid = ParseThemeColor(field.value, preview);
        rows.push_back(hbox({
            text(" " + field.label) | size(WIDTH, EQUAL, 27) | color(theme.modal_text_color),
            inputs_[index]->Render() | size(WIDTH, EQUAL, 18),
            text("  ") | bgcolor(preview),
            text(valid ? "" : " invalid") |
                color(valid ? theme.modal_text_color : ftxui::Color::Red),
        }));
    }
    return vbox(std::move(rows));
}

ftxui::Element ThemeColorsModalContent::RenderCustomFooter() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();

    // Divided footer text: status on the left, theme info on the right.
    std::string theme_info = "Theme: " +
        (theme_ ? theme_->name : std::string("active theme"));

    ftxui::Elements actions;
    actions.push_back(apply_button_->Render());
    actions.push_back(text(" "));
    actions.push_back(reset_button_->Render());
    actions.push_back(text(" "));
    actions.push_back(close_button_->Render());

    return vbox({
        hbox({
            text(" " + status_) | dim | color(theme.modal_text_color),
            filler(),
            text(" " + theme_info + " ") | dim | color(theme.modal_text_color),
        }),
        separator() | color(theme.modal_border),
        hbox(std::move(actions)),
    }) | bgcolor(theme.modal_background) | color(theme.modal_text_color);
}

ftxui::Element ThemeColorsModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        RenderTabs(),
        separator() | color(theme.modal_border),
        RenderGroup() | flex,
    }) | color(theme.modal_text_color);
}

ThemeColorsModal::ThemeColorsModal(
    const Theme* theme,
    ApplyCallback apply_callback,
    ResetCallback reset_callback,
    CloseCallback close_callback)
    : close_callback_(std::move(close_callback)) {
    content_ = std::make_shared<ThemeColorsModalContent>(
        theme, std::move(apply_callback), std::move(reset_callback),
        [this] { RequestClose(); });
    modal_ = std::make_shared<ModalWindow>(
        content_, theme, [this] { RequestClose(); });
    modal_->SetBodyFrameScrolling(false);
}

void ThemeColorsModal::Open() {
    open_ = true;
    content_->Open();
}

void ThemeColorsModal::RequestClose() {
    if (!open_) {
        return;
    }
    open_ = false;
    if (close_callback_) {
        close_callback_();
    }
}

} // namespace textlt
