#include "modal_custom_processor_builder.hpp"

#include "app_resources.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include "ui_button.hpp"

namespace textlt {
namespace {

ButtonSpec CustomProcessorButtonSpec(std::string label) {
    ButtonSpec spec;
    spec.caption = std::move(label);
    spec.variant = ButtonVariant::AccentEdges;

    const std::string& caption = spec.caption;
    if (caption == "Save Processor" || caption == "Copy AI Prompt" ||
        caption == "Paste JSON" || caption == "Validate") {
        spec.role = ButtonRole::Primary;
        return spec;
    }
    if (caption == "Close") {
        spec.role = ButtonRole::Cancel;
        return spec;
    }
    if (caption == "Create" || caption == "Manage") {
        spec.role = ButtonRole::Tab;
        return spec;
    }
    if (caption == "Delete") {
        spec.role = ButtonRole::Danger;
        return spec;
    }
    if (caption == "Clear") {
        spec.role = ButtonRole::Warning;
        return spec;
    }
    if (caption == "Reload") {
        spec.role = ButtonRole::Utility;
        return spec;
    }
    if (caption == "Edit") {
        spec.role = ButtonRole::Secondary;
        return spec;
    }

    spec.role = ButtonRole::Toggle;
    spec.variant = ButtonVariant::Minimal;
    spec.size = ButtonSize::Compact;
    return spec;
}

ftxui::Element TextButtonElement(const std::string& label,
                                 const Theme& theme,
                                 bool focused,
                                 bool selected = false) {
    ButtonSpec spec = CustomProcessorButtonSpec(label);
    if (spec.role == ButtonRole::Tab) {
        return RenderModalTabButton(theme, label, selected, focused);
    }
    spec.selected = selected;
    return RenderButton(theme, spec, focused);
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
        return TextButtonElement(state.label, theme, state.focused || state.active, state.state);
    };

    tab_buttons_ = ftxui::Menu(&tab_labels_, &selected_tab_, menu_option);

    group_row1_size_ = static_cast<int>(group_labels_.size() + 1) / 2;
    group_labels_row1_.assign(group_labels_.begin(),
                              group_labels_.begin() + group_row1_size_);
    group_labels_row2_.assign(group_labels_.begin() + group_row1_size_,
                              group_labels_.end());

    auto row1_option = menu_option;
    row1_option.on_enter = [this] {
        selected_group_ = selected_group_row1_;
    };
    auto row2_option = menu_option;
    row2_option.on_enter = [this] {
        selected_group_ = group_row1_size_ + selected_group_row2_;
    };

    group_menu_row1_ = ftxui::Menu(&group_labels_row1_, &selected_group_row1_, row1_option);
    group_menu_row2_ = ftxui::Menu(&group_labels_row2_, &selected_group_row2_, row2_option);
    scope_menu_ = ftxui::Menu(&scope_labels_, &selected_scope_, menu_option);
    output_menu_ = ftxui::Menu(&output_labels_, &selected_output_, menu_option);

    ftxui::InputOption request_option;
    request_option.multiline = true;
    request_option.cursor_position = &request_cursor_;
    request_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    request_input_ = ftxui::Input(&request_text_, "Describe what this processor should do", request_option);

    ftxui::InputOption json_option;
    json_option.multiline = true;
    json_option.cursor_position = &json_cursor_;
    json_option.transform = [this](ftxui::InputState state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return theme.InputTransform(std::move(state));
    };
    json_input_ = ftxui::Input(&json_text_, "Paste AI JSON object here", json_option);

    auto processor_option = ftxui::MenuOption::Vertical();
    processor_option.entries_option.transform = [this](const ftxui::EntryState& state) {
        const Theme& theme = theme_ ? *theme_ : FallbackTheme();
        return TextButtonElement(state.label, theme, state.focused || state.active, state.state);
    };
    processor_labels_ = {"No editable user processors"};
    processor_menu_ = ftxui::Menu(&processor_labels_, &selected_processor_, processor_option);

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
    manage_close_button_ = MakeTextButton("Close", [this] {
        if (close_callback_) {
            close_callback_();
        }
    });
    edit_processor_button_ = MakeTextButton("Edit", [this] { EditSelectedProcessor(); });
    delete_processor_button_ = MakeTextButton("Delete", [this] { DeleteSelectedProcessor(); });
    reload_processors_button_ = MakeTextButton("Reload", [this] { ReloadManageList(); });

    button_container_ = ftxui::Container::Horizontal({
        copy_prompt_button_,
        paste_json_button_,
        validate_button_,
        save_button_,
        clear_button_,
        close_button_,
    });

    manage_button_container_ = ftxui::Container::Horizontal({
        edit_processor_button_,
        delete_processor_button_,
        reload_processors_button_,
        manage_close_button_,
    });

    create_tab_container_ = ftxui::Container::Vertical({
        group_menu_row1_,
        group_menu_row2_,
        scope_menu_,
        output_menu_,
        request_input_,
        json_input_,
        button_container_,
    });

    manage_tab_container_ = ftxui::Container::Vertical({
        processor_menu_,
        manage_button_container_,
    });

    tab_body_container_ = ftxui::Container::Tab({
        create_tab_container_,
        manage_tab_container_,
    }, &selected_tab_);

    container_ = ftxui::Container::Vertical({
        tab_buttons_,
        tab_body_container_,
    });
}

ftxui::Component CustomProcessorBuilderModalContent::MakeTextButton(
    std::string label,
    std::function<void()> on_click) {
    ButtonSpec spec = CustomProcessorButtonSpec(std::move(label));
    return MakeButton(theme_, std::move(spec), std::move(on_click));
}

void CustomProcessorBuilderModalContent::Open() {
    selected_tab_ = 0;
    status_ = "Describe the processor, copy the AI prompt, paste the JSON result, then save.";
    status_is_error_ = false;
    pending_delete_id_.clear();
    selected_group_row1_ = std::clamp(selected_group_, 0, group_row1_size_ - 1);
    selected_group_row2_ = std::clamp(selected_group_ - group_row1_size_, 0,
                                      std::max(0, static_cast<int>(group_labels_row2_.size()) - 1));
    ReloadManageList();
    selected_tab_ = 0;
    if (!status_is_error_) {
        status_ = "Describe the processor, copy the AI prompt, paste the JSON result, then save.";
    }
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
    int index = selected_group_;
    if (index < 0 || index >= static_cast<int>(group_labels_.size())) {
        return "User";
    }
    return group_labels_[index];
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

    ReloadManageList();
    status_ = "Saved custom processor: " + result.name + " (" + result.id + ").";
    status_is_error_ = false;
}

void CustomProcessorBuilderModalContent::ClearFields() {
    request_text_.clear();
    json_text_.clear();
    request_cursor_ = 0;
    json_cursor_ = 0;
    selected_group_ = 0;
    selected_group_row1_ = 0;
    selected_group_row2_ = 0;
    has_last_copied_prompt_request_ = false;
    last_copied_prompt_request_ = CustomProcessorPromptRequest{};
    status_ = "Cleared custom processor builder fields.";
    status_is_error_ = false;
    if (request_input_) {
        request_input_->TakeFocus();
    }
}


void CustomProcessorBuilderModalContent::ReloadManageList() {
    std::string error;
    const std::filesystem::path default_processors_directory = FindTextProcessorsDirectory();
    if (!default_processors_directory.empty()) {
        TextParserManager manager;
        (void)manager.EnsureUserConfiguration(default_processors_directory, error);
    }
    if (error.empty()) {
        editable_processors_ = ListEditableCustomProcessors(error);
    } else {
        editable_processors_.clear();
    }
    processor_labels_.clear();
    if (!error.empty()) {
        processor_labels_.push_back("Cannot load custom processors");
        selected_processor_ = 0;
        status_ = error;
        status_is_error_ = true;
        pending_delete_id_.clear();
        return;
    }

    if (editable_processors_.empty()) {
        processor_labels_.push_back("No editable user processors");
        selected_processor_ = 0;
    } else {
        for (const CustomProcessorSummary& processor : editable_processors_) {
            const std::string name = processor.name.empty() ? processor.id : processor.name;
            processor_labels_.push_back(name + " (" + processor.id + ")");
        }
        selected_processor_ = std::clamp(
            selected_processor_, 0, static_cast<int>(editable_processors_.size()) - 1);
    }

    pending_delete_id_.clear();
    status_ = "Loaded editable custom processors.";
    status_is_error_ = false;
}

bool CustomProcessorBuilderModalContent::HasSelectedProcessor() const {
    return selected_processor_ >= 0 &&
           selected_processor_ < static_cast<int>(editable_processors_.size());
}

const CustomProcessorSummary* CustomProcessorBuilderModalContent::SelectedProcessor() const {
    if (!HasSelectedProcessor()) {
        return nullptr;
    }
    return &editable_processors_[static_cast<size_t>(selected_processor_)];
}

void CustomProcessorBuilderModalContent::EditSelectedProcessor() {
    const CustomProcessorSummary* selected = SelectedProcessor();
    if (selected == nullptr) {
        status_ = "Select an editable user processor first.";
        status_is_error_ = true;
        return;
    }

    const CustomProcessorLoadResult result = LoadCustomProcessorForEditing(selected->id);
    if (!result.success) {
        status_ = result.error.empty() ? "Cannot load processor for editing." : result.error;
        status_is_error_ = true;
        return;
    }

    request_text_ = "Editing existing processor: " + result.id;
    request_cursor_ = static_cast<int>(request_text_.size());
    json_text_ = result.json_text;
    json_cursor_ = static_cast<int>(json_text_.size());
    selected_tab_ = 0;
    pending_delete_id_.clear();
    status_ = "Loaded processor for editing: " + result.name + " (" + result.id + ").";
    status_is_error_ = false;
    if (json_input_) {
        json_input_->TakeFocus();
    }
}

void CustomProcessorBuilderModalContent::DeleteSelectedProcessor() {
    const CustomProcessorSummary* selected = SelectedProcessor();
    if (selected == nullptr) {
        status_ = "Select an editable user processor first.";
        status_is_error_ = true;
        return;
    }

    if (pending_delete_id_ != selected->id) {
        pending_delete_id_ = selected->id;
        status_ = "Press Delete again to confirm deleting " + selected->id + ".";
        status_is_error_ = true;
        return;
    }

    const CustomProcessorDeleteResult result = DeleteCustomProcessor(selected->id);
    pending_delete_id_.clear();
    if (!result.success) {
        status_ = result.error.empty() ? "Cannot delete custom processor." : result.error;
        status_is_error_ = true;
        ReloadManageList();
        if (result.error.empty()) {
            status_ = "Cannot delete custom processor.";
            status_is_error_ = true;
        }
        return;
    }

    ReloadManageList();
    status_ = "Deleted custom processor: " + result.name + " (" + result.id + ").";
    status_is_error_ = false;
}

bool CustomProcessorBuilderModalContent::HandleEvent(ftxui::Event event) {
    if (event == ftxui::Event::Escape) {
        if (close_callback_) {
            close_callback_();
        }
        return true;
    }

    if (selected_tab_ == 1) {
        if (event == ftxui::Event::Return || event.input() == "\x0A") {
            EditSelectedProcessor();
            return true;
        }
        if (event == ftxui::Event::Delete) {
            DeleteSelectedProcessor();
            return true;
        }
        if (event.input() == "r" || event.input() == "R") {
            ReloadManageList();
            return true;
        }
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
            pending_delete_id_.clear();
        }
    }

    return false;
}

ftxui::Element CustomProcessorBuilderModalContent::RenderTitle() {
    return ftxui::text(GetTitle());
}


ftxui::Element CustomProcessorBuilderModalContent::RenderTabs() const {
    using namespace ftxui;
    return tab_buttons_->Render();
}

ftxui::Element CustomProcessorBuilderModalContent::RenderCreateTab() const {
    using namespace ftxui;
    return vbox({
        RenderMetadata(),
        separator(),
        RenderInputs(),
        separator(),
        RenderActions(),
        separator(),
        RenderHelp(),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderManageTab() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        hbox({
            vbox({
                SectionTitle("Editable user Lua processors", theme),
                processor_menu_->Render() |
                    border |
                    size(WIDTH, EQUAL, 48) |
                    size(HEIGHT, EQUAL, 12),
            }),
            text(" "),
            RenderProcessorDetails() |
                border |
                size(WIDTH, EQUAL, 58) |
                size(HEIGHT, EQUAL, 12),
        }),
        separator(),
        manage_button_container_->Render(),
        separator(),
        text("Enter/Edit loads selected processor into Create tab. Delete requires pressing Delete twice.") |
            dim |
            color(theme.modal_text_color),
        text("Only user_ Lua processors are editable. Built-in and locked processors are hidden.") |
            dim |
            color(theme.modal_text_color),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderProcessorDetails() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const CustomProcessorSummary* selected = SelectedProcessor();
    if (selected == nullptr) {
        return vbox({
            SectionTitle("Details", theme),
            text("No editable processor selected.") | color(theme.modal_text_color),
        });
    }

    return vbox({
        SectionTitle("Details", theme),
        text("id: " + selected->id) | color(theme.modal_text_color),
        text("name: " + selected->name) | color(theme.modal_text_color),
        text("group: " + selected->group) | color(theme.modal_text_color),
        text("scope: " + selected->scope) | color(theme.modal_text_color),
        text("output: " + selected->output) | color(theme.modal_text_color),
        text("script: " + selected->script_path.generic_string()) |
            color(theme.modal_text_color),
        separator(),
        paragraph(selected->description.empty() ? "No description." : selected->description) |
            color(theme.modal_text_color),
    });
}

ftxui::Element CustomProcessorBuilderModalContent::RenderMetadata() const {
    using namespace ftxui;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    return vbox({
        SectionTitle("Metadata", theme),
        hbox({text("Group: ") | size(WIDTH, EQUAL, 8), group_menu_row1_->Render()}),
        hbox({text("         ") | size(WIDTH, EQUAL, 8), group_menu_row2_->Render()}),
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
                size(HEIGHT, EQUAL, 9) |
                yframe,
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
        RenderTabs(),
        separator(),
        selected_tab_ == 1 ? RenderManageTab() : RenderCreateTab(),
        separator(),
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
