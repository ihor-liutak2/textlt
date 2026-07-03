#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "custom_processor_builder.hpp"
#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "theme.hpp"

namespace textlt {

class CustomProcessorBuilderModalContent : public IModalContent {
public:
    using ReadClipboardCallback = std::function<std::string()>;
    using WriteClipboardCallback = std::function<void(const std::string& text)>;
    using CloseCallback = std::function<void()>;

    CustomProcessorBuilderModalContent(
        const Theme* theme,
        ReadClipboardCallback read_clipboard,
        WriteClipboardCallback write_clipboard,
        CloseCallback close_callback);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Custom Processor Builder"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {112, 36}; }
    ModalFrameStyle GetModalFrameStyle() const override { return ModalFrameStyle::TitleInBorder; }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();
    bool HandleEvent(ftxui::Event event);

private:
    ftxui::Component MakeTextButton(std::string label, std::function<void()> on_click);
    CustomProcessorPromptRequest CurrentPromptRequest() const;
    void CopyPrompt();
    void PasteJson();
    void ValidateJson();
    void SaveProcessor();
    void ClearFields();
    std::string SelectedGroup() const;
    std::string SelectedScope() const;
    std::string SelectedOutput() const;
    ftxui::Element RenderMetadata() const;
    ftxui::Element RenderInputs() const;
    ftxui::Element RenderActions() const;
    ftxui::Element RenderHelp() const;
    ftxui::Element RenderStatus() const;

    const Theme* theme_ = nullptr;
    ReadClipboardCallback read_clipboard_;
    WriteClipboardCallback write_clipboard_;
    CloseCallback close_callback_;

    std::vector<std::string> group_labels_;
    std::vector<std::string> group_labels_row1_;
    std::vector<std::string> group_labels_row2_;
    std::vector<std::string> scope_labels_;
    std::vector<std::string> output_labels_;
    int selected_group_ = 0;
    int selected_group_row1_ = 0;
    int selected_group_row2_ = 0;
    int selected_scope_ = 0;
    int selected_output_ = 0;
    int group_row1_size_ = 0;

    std::string request_text_;
    std::string json_text_;
    int request_cursor_ = 0;
    int json_cursor_ = 0;
    std::string status_ = "Describe the processor, copy the AI prompt, paste the JSON result, then save.";
    bool status_is_error_ = false;
    CustomProcessorPromptRequest last_copied_prompt_request_;
    bool has_last_copied_prompt_request_ = false;

    ftxui::Component group_menu_;
    ftxui::Component group_menu_row1_;
    ftxui::Component group_menu_row2_;
    ftxui::Component scope_menu_;
    ftxui::Component output_menu_;
    ftxui::Component request_input_;
    ftxui::Component json_input_;
    ftxui::Component copy_prompt_button_;
    ftxui::Component paste_json_button_;
    ftxui::Component validate_button_;
    ftxui::Component save_button_;
    ftxui::Component clear_button_;
    ftxui::Component close_button_;
    ftxui::Component button_container_;
    ftxui::Component container_;
};

class CustomProcessorBuilderModal {
public:
    CustomProcessorBuilderModal(
        const Theme* theme,
        CustomProcessorBuilderModalContent::ReadClipboardCallback read_clipboard,
        CustomProcessorBuilderModalContent::WriteClipboardCallback write_clipboard);

    ftxui::Component View() const;
    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;
    CustomProcessorBuilderModalContent::ReadClipboardCallback read_clipboard_;
    CustomProcessorBuilderModalContent::WriteClipboardCallback write_clipboard_;
    std::shared_ptr<CustomProcessorBuilderModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
