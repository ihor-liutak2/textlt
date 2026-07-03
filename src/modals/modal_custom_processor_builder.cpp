#include "modal_custom_processor_builder.hpp"

#include "app_resources.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

namespace textlt {
namespace {

ftxui::Element TextButtonElement(const std::string& label, const Theme& theme, bool focused) {
    ftxui::Element button = ftxui::text("[" + label + "]");
    if (focused) {
        return button |
            ftxui::bgcolor(theme.modal_selected_item_bg) |
            ftxui::color(theme.modal_selected_item_fg);
    }
    return button | ftxui::color(theme.modal_accent);
}

ftxui::Element SectionTitle(const std::string& label, const Theme& theme) {
    return ftxui::text(" " + label + " ") |
        ftxui::bold |
        ftxui::color(theme.modal_accent);
}

} // namespace

CustomProcessorBuilderModalContent::CustomProcessorBuilderModalContent(
    const Theme* theme,
    ReadClipboardCallback read_clipboard,
    WriteClipboardCallback write_clipboard,
    CloseCallback close_callback)
    : theme_(theme),
      read_clipboard_(std::move(read_clipboard)),
      write_clipboard_(std::move(write_clipboard)),
      close_callback_(std::move(close_callback)),
      group_labels_(CustomProcessorGroupChoices()),
      scope_labels_({"Text", "Paragraph", "Code"}),
      output_labels_({"ReplaceText", "Report"}) {
    auto menu_option = ftxui::MenuOption::Toggle();
    menu_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return TextButtonElement(state.label, theme, state.focused || state.active);
    };

    group_menu_ = ftxui::Menu(&group_labels_, &selected_group_, menu_option);
    scope_menu_ = ftxui::Menu(&scope_labels_, &selected_scope_, menu_option);
    output_menu_ = ftxui::Menu(&output_labels_, &selected_output_, menu_option);

    ftxui::InputOption request_option;
    request_option.multiline = true;
    request_option.cursor_position = &request_cursor_;
    request_input_ = ftxui::Input(&request_text_, "Describe what this processor should do", request_option);

    ftxui::InputOption json_option;
    json_option.multiline = true;
    json_option.cursor_position = &json_cursor_;
    json_input_ = ftxui::Input(&json_text_, "Paste AI JSON object here", json_option);

    copy_prompt_button_ = MakeTextButton("Copy AI Prompt", [this] { CopyPrompt(); });
    paste_json_button_ = MakeTextButton("Paste JSON", [this] { PasteJson(); });
    validate_button_ = MakeTextButton("Validate", [this] { ValidateJson(); });
    save_button_ = MakeTextButton("Save Processor", [this] { SaveProcessor(); });
    clear_button_ = MakeTextButton("Clear", [this] { ClearFields(); });
    close_button_ = MakeTextButton("Close", [this] {
        if (close_callback_) {
            close_callback_();
        }
    });

    button_container_ = ftxui::Container::Horizontal({
        copy_prompt_button_,
        paste_json_button_,
        validate_button_,
        save_button_,
        clear_button_,
        close_button_,
    });

    container_ = ftxui::Container::Vertical({
        group_menu_,
        scope_menu_,
        output_menu_,
        request_input_,
        json_input_,
        button_container_,
    });
}

ftxui::Component CustomProcessorBuilderModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return TextButtonElement(state.label, theme, state.focused || state.active);
    };
    return ftxui::Button(std::move(option));
}

void CustomProcessorBuilderModalContent::Open() {
    status_ = "Describe the processor, copy the AI prompt, paste the JSON result, then save.";
    status_is_error_ = false;
    if (request_input_) {
        request_input_->TakeFocus();
    }
}

void CustomProcessorBuilderModalContent::Close() {}

CustomProcessorPromptRequest CustomProcessorBuilderModalContent::CurrentPromptRequest() const {
    CustomProcessorPromptRequest request;
    request.user_request = request_text_;
    request.group = SelectedGroup();
    request.scope = SelectedScope();
    request.output = SelectedOutput();
    return request;
}

std::string CustomProcessorBuilderModalContent::SelectedGroup() const {
    if (selected_group_ < 0 || selected_group_ >= static_cast<int>(group_labels_.size())) {
        return "User";
    }
    return group_labels_[selected_group_];
}

std::string CustomProcessorBuilderModalContent::SelectedScope() const {
    if (selected_scope_ == 1) {
        return "paragraph";
    }
    if (selected_scope_ == 2) {
        return "code";
    }
    return "text";
}

std::string CustomProcessorBuilderModalContent::SelectedOutput() const {
    return selected_output_ == 1 ? "report" : "replace_text";
}

void CustomProcessorBuilderModalContent::CopyPrompt() {
    if (!write_clipboard_) {
        status_ = "Clipboard write is not configured.";
        status_is_error_ = true;
        return;
    }
    if (request_text_.empty()) {
        status_ = "Describe what the processor should do first.";
        status_is_error_ = true;
        return;
    }

    last_copied_prompt_request_ = CurrentPromptRequest();
    has_last_copied_prompt_request_ = true;
    write_clipboard_(BuildCustomProcessorAiPrompt(last_copied_prompt_request_));
    status_ = "Copied AI prompt. Paste it into an AI chat, then paste the JSON result back here.";
    status_is_error_ = false;
}

void CustomProcessorBuilderModalContent::PasteJson() {
    if (!read_clipboard_) {
        status_ = "Clipboard read is not configured.";
        status_is_error_ = true;
        return;
    }
    json_text_ = read_clipboard_();
    json_cursor_ = static_cast<int>(json_text_.size());
    if (json_text_.empty()) {
        status_ = "Clipboard is empty.";
        status_is_error_ = true;
        return;
    }
    status_ = "Pasted JSON candidate from clipboard.";
    status_is_error_ = false;
}

void CustomProcessorBuilderModalContent::ValidateJson() {
    const CustomProcessorInstallResult result = ValidateCustomProcessorJson(json_text_);
    if (!result.success) {
        const std::string error = result.error.empty() ? "JSON validation failed." : result.error;
        if (write_clipboard_) {
            CustomProcessorPromptRequest repair_request = CurrentPromptRequest();
            if (repair_request.user_request.find_first_not_of(" \t\r\n") == std::string::npos &&
                has_last_copied_prompt_request_) {
                repair_request = last_copied_prompt_request_;
            }
            write_clipboard_(BuildCustomProcessorRepairPrompt(repair_request, json_text_, error));
            status_ = error + " Repair prompt copied to clipboard.";
        } else {
            status_ = error + " Clipboard write is not configured, so repair prompt was not copied.";
        }
        status_is_error_ = true;
        return;
    }
    status_ = "Valid processor JSON: " + result.name + " (" + result.id + ").";
    status_is_error_ = false;
}

void CustomProcessorBuilderModalContent::SaveProcessor() {
    const std::filesystem::path default_processors_directory = FindTextProcessorsDirectory();
    if (default_processors_directory.empty()) {
        status_ = "Text processor resources were not found. Reinstall TextLT or set TEXTLT_DATA_DIR.";
        status_is_error_ = true;
        return;
    }

    const CustomProcessorInstallResult result =
        InstallCustomProcessorFromJson(json_text_, default_processors_directory);
    if (!result.success) {
        status_ = result.error.empty() ? "Cannot save custom processor." : result.error;
        status_is_error_ = true;
        return;
    }

    status_ = "Saved custom processor: " + result.name + " (" + result.id + ").";
    status_is_error_ = false;
}

void CustomProcessorBuilderModalContent::ClearFields() {
    request_text_.clear();
    json_text_.clear();
    request_cursor_ = 0;
    json_cursor_ = 0;
    has_last_copied_prompt_request_ = false;
    last_copied_prompt_request_ = CustomProcessorPromptRequest{};
    status_ = "Cleared custom processor builder fields.";
    status_is_error_ = false;
    if (request_input_) {
        request_input_->TakeFocus();
    }
}

bool CustomProcessorBuilderModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        if (close_callback_) {
            close_callback_();
        }
        return true;
    }
    return false;
}

ftxui::Element CustomProcessorBuilderModalContent::RenderTitle() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        text(" Custom Processor Builder ") | bold | color(theme.modal_foreground),
        filler(),
        text("AI prompt → JSON → local Lua processor") | color(theme.modal_text_color),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderMetadata() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        SectionTitle("Metadata", theme),
        hbox({text("Group: ") | size(WIDTH, EQUAL, 8), group_menu_->Render()}),
        hbox({text("Scope: ") | size(WIDTH, EQUAL, 8), scope_menu_->Render()}),
        hbox({text("Output: ") | size(WIDTH, EQUAL, 8), output_menu_->Render()}),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderInputs() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return hbox({
        vbox({
            SectionTitle("Request", theme),
            request_input_->Render() |
                border |
                size(WIDTH, EQUAL, 52) |
                size(HEIGHT, EQUAL, 9),
        }),
        text(" "),
        vbox({
            SectionTitle("AI JSON result", theme),
            json_input_->Render() |
                border |
                size(WIDTH, EQUAL, 52) |
                size(HEIGHT, EQUAL, 9),
        }),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderActions() const {
    using namespace ftxui;
    return hbox({
        copy_prompt_button_->Render(),
        text(" "),
        paste_json_button_->Render(),
        text(" "),
        validate_button_->Render(),
        text(" "),
        save_button_->Render(),
        text(" "),
        clear_button_->Render(),
        filler(),
        close_button_->Render(),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderHelp() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        text("Flow: write request → Copy AI Prompt → ask AI → paste JSON code block content → Validate → Save Processor") |
            color(theme.modal_text_color),
        text("Saved processors go to ~/.config/textlt/text_parsers.json and ~/.config/textlt/text_parsers/<id>.lua") |
            dim |
            color(theme.modal_text_color),
        text("Lua is sandboxed: no files, shell, network, require, io, os, package, debug or clipboard.") |
            dim |
            color(theme.modal_text_color),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderStatus() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return text(status_) |
        color(status_is_error_ ? theme.modal_accent : theme.modal_text_color);
}

ftxui::Element CustomProcessorBuilderModalContent::Render() {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        RenderMetadata(),
        separator(),
        RenderInputs(),
        separator(),
        RenderActions(),
        separator(),
        RenderHelp(),
        RenderStatus(),
    }) | color(theme.modal_text_color);
}

CustomProcessorBuilderModal::CustomProcessorBuilderModal(
    const Theme* theme,
    CustomProcessorBuilderModalContent::ReadClipboardCallback read_clipboard,
    CustomProcessorBuilderModalContent::WriteClipboardCallback write_clipboard)
    : theme_(theme),
      read_clipboard_(std::move(read_clipboard)),
      write_clipboard_(std::move(write_clipboard)) {
    content_ = std::make_shared<CustomProcessorBuilderModalContent>(
        theme_,
        read_clipboard_,
        write_clipboard_,
        [this] { Close(); });
    modal_ = std::make_shared<ModalWindow>(content_, theme_, [this] { Close(); });
}

ftxui::Component CustomProcessorBuilderModal::View() const {
    return modal_;
}

void CustomProcessorBuilderModal::Open() {
    open_ = true;
    if (content_) {
        content_->SetTheme(theme_);
        content_->Open();
    }
    if (modal_) {
        modal_->SetTheme(theme_);
        modal_->SetModalSize(112, 36);
    }
}

void CustomProcessorBuilderModal::Close() {
    open_ = false;
    if (content_) {
        content_->Close();
    }
}

bool CustomProcessorBuilderModal::IsOpen() const {
    return open_;
}

bool CustomProcessorBuilderModal::OnEvent(ftxui::Event event) {
    if (!open_ || !modal_) {
        return false;
    }
    if (content_ && content_->HandleEvent(event)) {
        return true;
    }
    return modal_->OnEvent(event);
}

} // namespace textlt
