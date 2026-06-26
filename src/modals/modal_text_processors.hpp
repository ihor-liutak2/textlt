#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "text_parser_manager.hpp"
#include "theme.hpp"

namespace textlt {

class TextProcessorsModalContent : public IModalContent {
public:
    using TargetTextProvider = std::function<bool(
        bool whole_document,
        std::string& text,
        std::string& error)>;
    using ReplaceTargetCallback = std::function<bool(
        bool whole_document,
        const std::string& text,
        std::string& error)>;
    using CloseCallback = std::function<void()>;

    TextProcessorsModalContent(
        const Theme* theme,
        TargetTextProvider target_text_provider,
        ReplaceTargetCallback replace_target_text,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Text Processors"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {96, 30}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return status_; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();
    void Reload();
    void ApplySelected();
    bool HandleEvent(ftxui::Event event);

private:
    ftxui::Component MakeTextButton(
        std::string label,
        std::function<void()> on_click);

    void SetScope(TextParserScope scope);
    TextParserScope CurrentScope() const;
    void RebuildParserList();
    void SyncParamsFromSelection();
    const TextParserDefinition* SelectedParser() const;
    std::unordered_map<std::string, std::string> CurrentParams() const;
    int RepeatCount() const;

    ftxui::Element RenderScopeTabs() const;
    ftxui::Element RenderParserList() const;
    ftxui::Element RenderSelectedParserInfo() const;
    ftxui::Element RenderParameterFields() const;
    ftxui::Element RenderActionRow() const;
    ftxui::Element RenderHelpLine() const;

    std::string TrimForDisplay(const std::string& text, size_t max_size) const;
    std::string ScopeLabel(TextParserScope scope) const;

    const Theme* theme_ = nullptr;
    TargetTextProvider target_text_provider_;
    ReplaceTargetCallback replace_target_text_;
    CloseCallback on_close_;

    TextParserManager manager_;
    TextParserScope active_scope_ = TextParserScope::Text;
    std::vector<const TextParserDefinition*> filtered_parsers_;
    std::vector<std::string> parser_labels_;
    int selected_parser_ = 0;

    bool whole_document_ = false;
    std::string repeat_count_ = "1";
    int repeat_cursor_ = 1;
    std::vector<std::string> param_values_ = {"", "", "", ""};
    std::vector<int> param_cursors_ = {0, 0, 0, 0};
    std::string status_ = "Select a text processor.";

    ftxui::Component text_tab_button_;
    ftxui::Component paragraph_tab_button_;
    ftxui::Component parser_menu_;
    ftxui::Component parser_list_component_;
    std::vector<ftxui::Component> param_inputs_;
    ftxui::Component repeat_input_;
    ftxui::Component whole_document_checkbox_;
    ftxui::Component apply_button_;
    ftxui::Component reload_button_;
    ftxui::Component close_button_;
    ftxui::Component container_;
};

class TextProcessorsModal {
public:
    using TargetTextProvider = TextProcessorsModalContent::TargetTextProvider;
    using ReplaceTargetCallback = TextProcessorsModalContent::ReplaceTargetCallback;

    TextProcessorsModal(
        const Theme* theme,
        TargetTextProvider target_text_provider,
        ReplaceTargetCallback replace_target_text);

    ftxui::Component View() const;

    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;

    std::shared_ptr<TextProcessorsModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
