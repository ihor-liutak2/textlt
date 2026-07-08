#include "modal_distraction_options.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

const Theme& ResolveTheme(const Theme* theme) {
    static const Theme fallback_theme = FallbackTheme();
    return theme ? *theme : fallback_theme;
}

std::string ColumnModeLabel(int column_count) {
    return column_count == 2 ? "2 columns" : "1 column";
}

} // namespace

DistractionOptionsContent::DistractionOptionsContent(
    const Theme* theme,
    SettingsProvider settings_provider,
    ApplySettingsCallback on_apply_settings,
    CommandCallback on_command,
    CloseCallback on_close)
    : theme_(theme),
      settings_provider_(std::move(settings_provider)),
      on_apply_settings_(std::move(on_apply_settings)),
      on_command_(std::move(on_command)),
      on_close_(std::move(on_close)) {
    mode_tab_button_ = MakeTabButton("Mode", 0);
    layout_tab_button_ = MakeTabButton("Layout", 1);
    tab_buttons_ = ftxui::Container::Horizontal({mode_tab_button_, layout_tab_button_});

    one_column_button_ = ftxui::Button(MakeButtonOption("1 column", [this] {
        SetColumnCount(1);
    }, ButtonRole::Toggle));
    two_column_button_ = ftxui::Button(MakeButtonOption("2 columns", [this] {
        SetColumnCount(2);
    }, ButtonRole::Toggle));
    enter_button_ = ftxui::Button(MakeButtonOption("Enter", [this] {
        ApplyDraft();
        if (on_command_) {
            on_command_("distraction.enter");
        }
    }, ButtonRole::Primary));
    apply_button_ = ftxui::Button(MakeButtonOption("Apply", [this] {
        ApplyInputsToDraft();
        ApplyDraft();
    }, ButtonRole::Primary));
    close_button_ = ftxui::Button(MakeButtonOption("Close", [this] {
        if (on_close_) {
            on_close_();
        }
    }, ButtonRole::Cancel));

    auto make_input = [this](std::string* value, int* cursor) {
        ftxui::InputOption option;
        option.multiline = false;
        option.cursor_position = cursor;
        option.on_enter = [this] {
            ApplyInputsToDraft();
            ApplyDraft();
        };
        option.transform = [this](ftxui::InputState state) {
            return ResolveTheme(theme_).InputTransform(std::move(state));
        };
        return ftxui::Input(value, "", option);
    };
    column_width_input_component_ = make_input(&column_width_input_, &column_width_cursor_);
    column_gap_input_component_ = make_input(&column_gap_input_, &column_gap_cursor_);
    top_padding_input_component_ = make_input(&top_padding_input_, &top_padding_cursor_);
    bottom_padding_input_component_ = make_input(&bottom_padding_input_, &bottom_padding_cursor_);

    mode_container_ = ftxui::Container::Horizontal({
        one_column_button_,
        two_column_button_,
    });
    layout_container_ = ftxui::Container::Vertical({
        column_width_input_component_,
        column_gap_input_component_,
        top_padding_input_component_,
        bottom_padding_input_component_,
    });
    tabs_container_ = ftxui::Container::Tab({mode_container_, layout_container_}, &active_tab_index_);
    footer_container_ = ftxui::Container::Horizontal({
        enter_button_,
        apply_button_,
        close_button_,
    });
    container_ = ftxui::Container::Vertical({tab_buttons_, tabs_container_, footer_container_});

    RefreshFromApp();
}

ftxui::ButtonOption DistractionOptionsContent::MakeButtonOption(
    std::string label,
    std::function<void()> on_click,
    ButtonRole role,
    std::string icon) const {
    ButtonSpec base_spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        ButtonVariant::Minimal,
        ButtonSize::Compact,
        std::move(icon));

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base_spec);
    option.on_click = std::move(on_click);
    option.transform = [this, base_spec](const ftxui::EntryState& state) {
        ButtonSpec spec = base_spec;
        if (spec.caption.find("1 column") != std::string::npos) {
            spec.selected = draft_.column_count == 1;
        } else if (spec.caption.find("2 columns") != std::string::npos) {
            spec.selected = draft_.column_count == 2;
        }
        return RenderModalFlatButton(ResolveTheme(theme_), spec, state.focused || state.active);
    };
    return option;
}

ftxui::Component DistractionOptionsContent::MakeTabButton(std::string label, int tab_index) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = "  " + label + "  ";
    option.on_click = [this, tab_index] { active_tab_index_ = tab_index; };
    option.transform = [this, tab_index, label = std::move(label)](const ftxui::EntryState& state) {
        return RenderModalTabButton(
            ResolveTheme(theme_),
            label,
            active_tab_index_ == tab_index,
            state.focused || state.active);
    };
    return ftxui::Button(option);
}

void DistractionOptionsContent::RefreshFromApp() {
    if (settings_provider_) {
        draft_ = DistractionModeController::NormalizeSettings(settings_provider_());
    } else {
        draft_ = DistractionModeController::NormalizeSettings({});
    }
    SyncInputsFromDraft();
}

void DistractionOptionsContent::TakeFocus() {
    if (mode_tab_button_) {
        mode_tab_button_->TakeFocus();
    }
}

void DistractionOptionsContent::SetColumnCount(int column_count) {
    draft_.column_count = column_count == 2 ? 2 : 1;
    draft_.column_width = draft_.column_count == 2 ? 72 : 92;
    draft_ = DistractionModeController::NormalizeSettings(draft_);
    SyncInputsFromDraft();
    ApplyDraft();
}

void DistractionOptionsContent::SyncInputsFromDraft() {
    column_width_input_ = std::to_string(draft_.column_width);
    column_gap_input_ = std::to_string(draft_.column_gap);
    top_padding_input_ = std::to_string(draft_.top_padding);
    bottom_padding_input_ = std::to_string(draft_.bottom_padding);
    column_width_cursor_ = static_cast<int>(column_width_input_.size());
    column_gap_cursor_ = static_cast<int>(column_gap_input_.size());
    top_padding_cursor_ = static_cast<int>(top_padding_input_.size());
    bottom_padding_cursor_ = static_cast<int>(bottom_padding_input_.size());
}

int DistractionOptionsContent::ParseInput(const std::string& value, int fallback) const {
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

void DistractionOptionsContent::ApplyInputsToDraft() {
    draft_.column_width = ParseInput(column_width_input_, draft_.column_width);
    draft_.column_gap = ParseInput(column_gap_input_, draft_.column_gap);
    draft_.top_padding = ParseInput(top_padding_input_, draft_.top_padding);
    draft_.bottom_padding = ParseInput(bottom_padding_input_, draft_.bottom_padding);
    draft_ = DistractionModeController::NormalizeSettings(draft_);
    SyncInputsFromDraft();
}

void DistractionOptionsContent::ApplyDraft() {
    if (on_apply_settings_) {
        on_apply_settings_(draft_);
    }
    status_ = "Applied " + ColumnModeLabel(draft_.column_count) +
        ", width " + std::to_string(draft_.column_width);
    if (draft_.column_count == 2) {
        status_ += ", gap " + std::to_string(draft_.column_gap);
    }
    status_ += ".";
}

ftxui::Element DistractionOptionsContent::RenderModeTab(const Theme& theme) {
    using namespace ftxui;

    return vbox({
        filler(),
        hbox({
            filler(),
            one_column_button_->Render(),
            text("  "),
            two_column_button_->Render(),
            filler(),
        }),
        filler(),
    }) | bgcolor(theme.modal_background);
}

ftxui::Element DistractionOptionsContent::RenderLayoutTab(const Theme& theme) {
    using namespace ftxui;

    auto input_box = [&](ftxui::Component input) {
        return input->Render() |
            size(WIDTH, EQUAL, 8) |
            borderStyled(LIGHT, theme.modal_border);
    };

    auto field = [&](const std::string& label, ftxui::Component input) {
        return hbox({
            vbox({
                text(""),
                text(" " + label) |
                    color(theme.modal_text_color) |
                    size(WIDTH, EQUAL, 17),
            }),
            input_box(input),
        });
    };

    Elements fields;
    fields.push_back(field("column width", column_width_input_component_));
    if (draft_.column_count == 2) {
        fields.push_back(field("column gap", column_gap_input_component_));
    }
    fields.push_back(field("top padding", top_padding_input_component_));
    fields.push_back(field("bottom padding", bottom_padding_input_component_));

    return vbox(std::move(fields)) | bgcolor(theme.modal_background);
}

ftxui::Element DistractionOptionsContent::Render() {
    using namespace ftxui;

    const Theme& theme = ResolveTheme(theme_);
    Element tab_body = active_tab_index_ == 0
        ? RenderModeTab(theme)
        : RenderLayoutTab(theme);

    return vbox({
        tab_buttons_->Render(),
        separator() | color(theme.modal_border),
        tab_body | flex,
    }) | bgcolor(theme.modal_background);
}

ftxui::Element DistractionOptionsContent::RenderCustomFooter() {
    using namespace ftxui;

    const Theme& theme = ResolveTheme(theme_);
    const bool enabled = draft_.enabled;

    Elements actions;
    if (active_tab_index_ == 0) {
        actions.push_back(enter_button_->Render());
    } else {
        actions.push_back(apply_button_->Render());
    }

    return vbox({
        hbox({
            text(" Status: ") | color(theme.modal_text_color),
            text(enabled ? "enabled" : "disabled") |
                bold |
                color(enabled ? theme.modal_accent : theme.modal_text_color),
            filler(),
        }),
        hbox({
            text(" " + status_) | color(theme.modal_text_color),
            filler(),
        }),
        separator() | color(theme.modal_border),
        hbox({
            hbox(std::move(actions)),
            filler(),
            close_button_->Render(),
        }),
    });
}

DistractionOptionsModal::DistractionOptionsModal(
    const Theme* theme,
    SettingsProvider settings_provider,
    ApplySettingsCallback on_apply_settings,
    CommandCallback on_command,
    CloseCallback on_close)
    : theme_(theme), on_close_(std::move(on_close)) {
    content_ = std::make_shared<DistractionOptionsContent>(
        theme_,
        std::move(settings_provider),
        std::move(on_apply_settings),
        std::move(on_command),
        [this] { RequestClose(); });
    modal_window_ = std::make_shared<ModalWindow>(
        content_,
        theme_,
        [this] { RequestClose(); });
    modal_window_->SetBodyFrameScrolling(false);
}

ftxui::Component DistractionOptionsModal::View() const {
    return modal_window_;
}

void DistractionOptionsModal::Open() {
    open_ = true;
    if (content_) {
        content_->RefreshFromApp();
    }
    TakeFocus();
}

void DistractionOptionsModal::Close() {
    open_ = false;
}

bool DistractionOptionsModal::IsOpen() const {
    return open_;
}

void DistractionOptionsModal::TakeFocus() {
    if (modal_window_) {
        modal_window_->TakeFocus();
    }
    if (content_) {
        content_->TakeFocus();
    }
}

void DistractionOptionsModal::SetTheme(const Theme* theme) {
    theme_ = theme;
    if (content_) {
        content_->SetTheme(theme_);
    }
    if (modal_window_) {
        modal_window_->SetTheme(theme_);
    }
}

void DistractionOptionsModal::RequestClose() {
    if (on_close_) {
        on_close_();
        return;
    }
    Close();
}

} // namespace textlt
