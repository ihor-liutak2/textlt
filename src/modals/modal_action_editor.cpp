#include "modals/modal_action_editor.hpp"

#include <utility>

#include "ftxui/component/component_options.hpp"

namespace textlt {
namespace {

ftxui::Element InputField(
    const std::string& label,
    const ftxui::Component& input,
    const Theme& theme,
    int height = 3) {
    using namespace ftxui;
    return vbox({
        text(label) | bold | color(theme.modal_accent),
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
    auto make_options = [this](int* cursor, bool multiline) {
        ftxui::InputOption option;
        option.cursor_position = cursor;
        option.multiline = multiline;
        option.transform = [this](ftxui::InputState state) {
            const Theme& resolved = theme_ ? *theme_ : FallbackTheme();
            return resolved.InputTransform(std::move(state));
        };
        return option;
    };
    title_input_ = ftxui::Input(
        &title_, "Short descriptive title", make_options(&title_cursor_, false));
    description_input_ = ftxui::Input(
        &description_, "Optional explanation",
        make_options(&description_cursor_, false));
    content_input_ = ftxui::Input(
        &content_, "Exact text copied to the clipboard",
        make_options(&content_cursor_, true));
    container_ = ftxui::Container::Vertical({
        title_input_, description_input_, content_input_,
    });
}

void ActionEditorModalContent::Begin(
    std::string id, std::string title, std::string description,
    std::string content) {
    id_ = std::move(id);
    title_ = std::move(title);
    description_ = std::move(description);
    content_ = std::move(content);
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
    return vbox({
        text("Action details") | bold | color(theme.modal_text_color),
        separator() | color(theme.modal_border),
        InputField("Title", title_input_, theme),
        text(""),
        InputField("Description", description_input_, theme),
        text(""),
        InputField("Content", content_input_, theme, 10) | flex,
    }) | size(WIDTH, EQUAL, 78) | color(theme.modal_text_color);
}

bool ActionEditorModalContent::Submit() {
    if (title_.empty()) {
        status_ = "Title is required.";
        title_input_->TakeFocus();
        return false;
    }
    actions::ActionItem item{id_, title_, description_, content_};
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
        content_->Begin(item->id, item->title, item->description, item->content);
    } else {
        content_->Begin({}, {}, {}, {});
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
