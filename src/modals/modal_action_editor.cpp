#include "modals/modal_action_editor.hpp"

#include <algorithm>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "ui_button.hpp"

namespace textlt {
namespace {

ftxui::Element InputField(
    const std::string& label,
    const ftxui::Component& input,
    const Theme& theme,
    ftxui::Box& clear_box,
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
    auto make_options = [this](
                            std::string* value,
                            int* cursor,
                            bool multiline,
                            bool limit_metadata) {
        ftxui::InputOption option;
        option.cursor_position = cursor;
        option.multiline = multiline;
        if (limit_metadata) {
            option.on_change = [value, cursor] {
                std::string limited = actions::LimitActionMetadata(*value);
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
    title_input_ = ftxui::Input(
        &title_, "Short descriptive title",
        make_options(&title_, &title_cursor_, false, true));
    description_input_ = ftxui::Input(
        &description_, "Optional explanation",
        make_options(&description_, &description_cursor_, false, true));
    content_input_ = ftxui::Input(
        &content_, "Exact text copied to the clipboard",
        make_options(&content_, &content_cursor_, true, false));

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
    hide_text_checkbox_ = ftxui::Checkbox("Hide text", &hide_text_, checkbox_option);

    container_ = ftxui::Container::Vertical({
        title_input_, description_input_, content_input_, hide_text_checkbox_,
    });
}

void ActionEditorModalContent::Begin(
    std::string id, std::string title, std::string description,
    std::string content, bool hide_text) {
    id_ = std::move(id);
    title_ = actions::LimitActionMetadata(title);
    description_ = actions::LimitActionMetadata(description);
    content_ = std::move(content);
    hide_text_ = hide_text;
    status_.clear();
    title_cursor_ = static_cast<int>(title_.size());
    description_cursor_ = static_cast<int>(description_.size());
    content_cursor_ = static_cast<int>(content_.size());
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
    description_clear_box_ = {1, 0, 1, 0};
    content_clear_box_ = {1, 0, 1, 0};
    return vbox({
        text("Action details") | bold | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        InputField("Title", title_input_, theme, title_clear_box_),
        text(""),
        InputField("Description", description_input_, theme, description_clear_box_),
        text(""),
        InputField("Content", content_input_, theme, content_clear_box_, 10) | flex,
        hide_text_checkbox_->Render(),
    }) | size(WIDTH, EQUAL, 78) | color(theme.modal_text_color);
}

bool ActionEditorModalContent::HandleEvent(ftxui::Event event) {
    if (!event.is_mouse()) return false;
    const ftxui::Mouse mouse = event.mouse();
    if (mouse.button != ftxui::Mouse::Left ||
        mouse.motion != ftxui::Mouse::Pressed) {
        return false;
    }
    if (title_clear_box_.Contain(mouse.x, mouse.y)) {
        title_.clear();
        title_cursor_ = 0;
        title_input_->TakeFocus();
        return true;
    }
    if (description_clear_box_.Contain(mouse.x, mouse.y)) {
        description_.clear();
        description_cursor_ = 0;
        description_input_->TakeFocus();
        return true;
    }
    if (content_clear_box_.Contain(mouse.x, mouse.y)) {
        content_.clear();
        content_cursor_ = 0;
        content_input_->TakeFocus();
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
    actions::ActionItem item{id_, title_, description_, content_, hide_text_};
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
    modal_ = std::make_shared<ModalWindow>(
        content_, theme, [this] { Close(); });
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
    if (item) {
        content_->Begin(item->id, item->title, item->description, item->content, item->hide_text);
    } else {
        content_->Begin({}, {}, {}, {}, false);
    }
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
    // Keep text editing independent from ModalWindow's remembered action
    // focus. Rebuilding a modal can otherwise leave a footer button active.
    if (!event.is_mouse() && event != ftxui::Event::Escape) {
        if (ftxui::Component form = content_->GetMainComponent();
            form && form->OnEvent(event)) {
            return true;
        }
    }
    return modal_->OnEvent(std::move(event));
}

} // namespace textlt
