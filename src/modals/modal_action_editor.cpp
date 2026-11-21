#include "modals/modal_action_editor.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

ftxui::Element InputField(const std::string& label,
    const ftxui::Component& input, const Theme& theme, ftxui::Box& clear_box,
    int height = 3) {
    using namespace ftxui;
    ButtonSpec clear_spec = ButtonSpecFromLabel(
        "Clear", ButtonRole::Warning, ButtonVariant::Minimal, ButtonSize::Compact);
    return vbox({
        hbox({
            text(label) | bold | color(theme.modal_accent) | xflex,
            RenderModalFlatButton(theme, clear_spec) | reflect(clear_box),
        }),
        input->Render() |
            color(theme.modal_input_fg) |
            bgcolor(theme.modal_input_bg) |
            borderStyled(LIGHT, theme.modal_border) |
            size(HEIGHT, EQUAL, height),
    });
}

} // namespace

ActionEditorModalContent::ActionEditorModalContent(
    const Theme* theme, SubmitCallback on_submit)
    : theme_(theme), on_submit_(std::move(on_submit)) {
    auto make_options = [this](std::string* value, int* cursor,
                               bool multiline, size_t metadata_limit) {
        ftxui::InputOption option;
        option.cursor_position = cursor;
        option.multiline = multiline;
        if (metadata_limit > 0) {
            option.on_change = [value, cursor, metadata_limit] {
                std::string limited = actions::LimitUtf8Characters(
                    *value, metadata_limit);
                if (limited.size() != value->size()) {
                    *value = std::move(limited);
                    *cursor = std::min(*cursor, static_cast<int>(value->size()));
                }
            };
        }
        option.transform = [this](ftxui::InputState state) {
            const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
            return resolved.InputTransform(std::move(state));
        };
        return option;
    };

    title_input_ = ftxui::Input(&title_, "Short descriptive title",
        make_options(&title_, &title_cursor_, false,
            actions::kActionMetadataCharacterLimit));
    for (int index = 0; index < 4; ++index) {
        const size_t tab = static_cast<size_t>(index);
        caption_inputs_[tab] = ftxui::Input(&captions_[tab], "Optional caption",
            make_options(&captions_[tab], &caption_cursors_[tab], false,
                actions::kActionCaptionCharacterLimit));
        content_inputs_[tab] = ftxui::Input(&contents_[tab],
            "Exact text copied to the clipboard",
            make_options(&contents_[tab], &content_cursors_[tab], true, 0));

        ftxui::CheckboxOption checkbox_option = ftxui::CheckboxOption::Simple();
        checkbox_option.transform = [this](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            ftxui::Element element = ftxui::text(
                std::string(state.state ? "[X] " : "[ ] ") + state.label);
            if (state.focused || state.active) {
                return element |
                    ftxui::bgcolor(theme.modal_selected_item_bg) |
                    ftxui::color(theme.modal_selected_item_fg);
            }
            return element | ftxui::color(theme.modal_text_color);
        };
        hide_text_checkboxes_[tab] = ftxui::Checkbox(
            "Hide text", &hide_text_[tab], checkbox_option);
        tab_forms_[tab] = ftxui::Container::Vertical({
            caption_inputs_[tab], content_inputs_[tab], hide_text_checkboxes_[tab],
        });
    }

    auto make_tab_button = [this](int index) {
        ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
        const std::string label = "Tab " + std::to_string(index + 1);
        option.label = label;
        option.on_click = [this, index] { SelectTab(index); };
        option.transform = [this, index, label](const ftxui::EntryState& state) {
            const Theme& theme = theme_ ? *theme_ : FallbackTheme();
            return RenderModalTabButton(theme, label,
                active_tab_ == index, state.focused || state.active);
        };
        return ftxui::Button(option);
    };
    std::vector<ftxui::Component> tab_button_components;
    std::vector<ftxui::Component> tab_form_components;
    for (int index = 0; index < 4; ++index) {
        tab_buttons_[static_cast<size_t>(index)] = make_tab_button(index);
        tab_button_components.push_back(tab_buttons_[static_cast<size_t>(index)]);
        tab_form_components.push_back(tab_forms_[static_cast<size_t>(index)]);
    }
    tab_button_row_ = ftxui::Container::Horizontal(std::move(tab_button_components));
    tab_container_ = ftxui::Container::Tab(
        std::move(tab_form_components), &active_tab_);
    container_ = ftxui::Container::Vertical({
        title_input_, tab_button_row_, tab_container_,
    });
}

void ActionEditorModalContent::SelectTab(int index) {
    active_tab_ = std::clamp(index, 0, 3);
}

void ActionEditorModalContent::Begin(const actions::ActionItem* item) {
    id_ = item ? item->id : std::string{};
    title_ = item ? actions::LimitActionMetadata(item->title) : std::string{};
    captions_ = {};
    contents_ = {};
    hide_text_ = {};
    if (item && !item->tabs.empty()) {
        for (size_t index = 0; index < std::min<size_t>(4, item->tabs.size()); ++index) {
            captions_[index] = actions::LimitActionCaption(item->tabs[index].caption);
            contents_[index] = item->tabs[index].content;
            hide_text_[index] = item->tabs[index].hide_text;
        }
    }
    active_tab_ = 0;
    status_.clear();
    title_cursor_ = static_cast<int>(title_.size());
    for (size_t index = 0; index < 4; ++index) {
        caption_cursors_[index] = static_cast<int>(captions_[index].size());
        content_cursors_[index] = static_cast<int>(contents_[index].size());
    }
    title_input_->TakeFocus();
}

std::string ActionEditorModalContent::GetTitle() {
    return EditingExisting() ? "Edit Action" : "Add Action";
}

ftxui::Element ActionEditorModalContent::RenderTitle() {
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return ftxui::text(" " + GetTitle() + " ") |
        ftxui::bold | ftxui::color(theme.modal_accent);
}

ftxui::Element ActionEditorModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    title_clear_box_ = {1, 0, 1, 0};
    caption_clear_boxes_ = {};
    content_clear_boxes_ = {};
    tab_boxes_ = {};
    Elements tab_buttons;
    for (size_t index = 0; index < tab_boxes_.size(); ++index) {
        if (index > 0) tab_buttons.push_back(text(" "));
        tab_buttons.push_back(RenderModalTabButton(theme,
            "Tab " + std::to_string(index + 1),
            active_tab_ == static_cast<int>(index)) | reflect(tab_boxes_[index]));
    }
    tab_buttons.push_back(filler());
    tab_buttons.push_back(text("Tabs 2–4 are optional") |
        dim | color(theme.modal_text_color));
    return vbox({
        text("Action details") | bold | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        InputField("Title", title_input_, theme, title_clear_box_),
        text(""),
        hbox(std::move(tab_buttons)),
        InputField("Caption", caption_inputs_[static_cast<size_t>(active_tab_)],
            theme, caption_clear_boxes_[static_cast<size_t>(active_tab_)]),
        text(""),
        InputField("Content", content_inputs_[static_cast<size_t>(active_tab_)],
            theme, content_clear_boxes_[static_cast<size_t>(active_tab_)], 10) | flex,
        hide_text_checkboxes_[static_cast<size_t>(active_tab_)]->Render(),
    }) | size(WIDTH, EQUAL, 78) | color(theme.modal_text_color);
}

bool ActionEditorModalContent::HandleEvent(ftxui::Event event) {
    if (!event.is_mouse()) return false;
    const ftxui::Mouse mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) return false;
    for (size_t index = 0; index < tab_boxes_.size(); ++index) {
        if (tab_boxes_[index].Contain(mouse.x, mouse.y)) {
            SelectTab(static_cast<int>(index));
            return true;
        }
    }
    if (title_clear_box_.Contain(mouse.x, mouse.y)) {
        title_.clear();
        title_cursor_ = 0;
        title_input_->TakeFocus();
        return true;
    }
    const size_t tab = static_cast<size_t>(active_tab_);
    if (caption_clear_boxes_[tab].Contain(mouse.x, mouse.y)) {
        captions_[tab].clear();
        caption_cursors_[tab] = 0;
        caption_inputs_[tab]->TakeFocus();
        return true;
    }
    if (content_clear_boxes_[tab].Contain(mouse.x, mouse.y)) {
        contents_[tab].clear();
        content_cursors_[tab] = 0;
        content_inputs_[tab]->TakeFocus();
        return true;
    }
    return false;
}

bool ActionEditorModalContent::Submit() {
    if (title_.empty()) {
        status_ = "Title is required.";
        title_input_->TakeFocus();
        return false;
    }
    size_t last_populated_tab = 0;
    for (size_t index = 1; index < 4; ++index) {
        if (!captions_[index].empty() || !contents_[index].empty() ||
            hide_text_[index]) last_populated_tab = index;
    }
    for (size_t index = 1; index <= last_populated_tab; ++index) {
        if (contents_[index].empty() &&
            (!captions_[index].empty() || hide_text_[index])) {
            status_ = "Tab " + std::to_string(index + 1) +
                " needs content, or clear its caption and hide option.";
            SelectTab(static_cast<int>(index));
            caption_inputs_[index]->TakeFocus();
            return false;
        }
    }

    std::vector<actions::ActionTab> tabs = {
        {captions_[0], contents_[0], hide_text_[0]},
    };
    for (size_t index = 1; index <= last_populated_tab; ++index) {
        tabs.push_back({captions_[index], contents_[index], hide_text_[index]});
    }
    actions::ActionItem item = actions::ActionItem::WithTabs(
        id_, title_, std::move(tabs));
    std::string error;
    if (!on_submit_ || !on_submit_(item, error)) {
        status_ = error.empty() ? "Could not save the action." : error;
        return false;
    }
    status_.clear();
    return true;
}

ActionEditorModal::ActionEditorModal(
    const Theme* theme, SubmitCallback on_submit) {
    content_ = std::make_shared<ActionEditorModalContent>(
        theme, std::move(on_submit));
    modal_ = std::make_shared<ModalWindow>(content_, theme, [this] { Close(); });
    modal_->SetHeaderCloseVisible(true);
    modal_->SetBodyFrameScrolling(false);
    ConfigureFooter();
}

void ActionEditorModal::ConfigureFooter() {
    modal_->SetFooterButtons({
        {"Cancel", [this] { Close(); }, ButtonRole::Cancel},
        {content_->EditingExisting() ? "Save" : "Add",
         [this] { if (content_->Submit()) Close(); }, ButtonRole::Primary},
    });
}

void ActionEditorModal::Open(const actions::ActionItem* item) {
    content_->Begin(item);
    ConfigureFooter();
    open_ = true;
    modal_->RefreshChildren();
    modal_->TakeFocus();
    content_->FocusFirstField();
}

void ActionEditorModal::Close() {
    open_ = false;
    content_->ClearStatus();
}

bool ActionEditorModal::OnEvent(ftxui::Event event) {
    if (!open_) return false;
    if (event.is_mouse() && content_->HandleEvent(event)) return true;
    if (!event.is_mouse() && event != ftxui::Event::Escape) {
        if (ftxui::Component form = content_->GetMainComponent();
            form && form->OnEvent(event)) return true;
    }
    return modal_->OnEvent(std::move(event));
}

} // namespace textlt
