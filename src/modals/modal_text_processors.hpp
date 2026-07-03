#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"

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
    ModalSizePreference GetModalSizePreference() const override { return {104, 34}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    std::string GetFooterText() const override { return ""; }

    void SetTheme(const Theme* theme) { theme_ = theme; }
    void Open();
    void Close();
    void Reload();
    void ApplySelected();
    void TogglePinned();
    bool HandleEvent(ftxui::Event event);

private:
    ftxui::Component MakeTextButton(
        std::string label,
        std::function<void()> on_click);

    void SetScope(TextParserScope scope);
    void SetGroup(std::string group);
    TextParserScope CurrentScope() const;
    void RebuildParserList();
    void RebuildParserListKeepingSelection(const std::string& parser_id);
    void SyncParamsFromSelection();
    const TextParserDefinition* SelectedParser() const;
    bool ValidateCurrentInputs(
        std::unordered_map<std::string, std::string>& params,
        int& repeat_count,
        std::string& error) const;
    int RepeatCountOrDefault() const;
    void MoveParserSelection(int row_delta, int column_delta = 0);
    void MoveParserSelectionToIndex(int index);
    void EnsureSelectionVisible();

    ftxui::Element RenderScopeTabs() const;
    ftxui::Element RenderGroupTabs() const;
    ftxui::Element RenderSelectedParserInfo() const;
    ftxui::Element RenderParameterFields() const;
    ftxui::Element RenderProcessorGrid() const;
    ftxui::Element RenderProcessorColumn(bool right_column, int visible_rows) const;
    ftxui::Element RenderProcessorCell(int parser_index, int width, int cell_slot) const;
    ftxui::Element RenderStatusLine() const;
    ftxui::Element RenderReportPreview() const;

    std::vector<std::string> WrapText(const std::string& text, size_t width) const;
    std::string TrimForDisplay(const std::string& text, size_t max_size) const;
    std::string ScopeLabel(TextParserScope scope) const;
    std::string ScopeDisplay(TextParserScope scope) const;
    std::vector<std::string> AvailableGroupsForCurrentScope() const;
    void EnsureActiveGroupIsAvailable();
    std::string NormalizedParamType(const std::string& type) const;
    std::string ParamHintText() const;

    const Theme* theme_ = nullptr;
    TargetTextProvider target_text_provider_;
    ReplaceTargetCallback replace_target_text_;
    CloseCallback on_close_;

    TextParserManager manager_;
    TextParserScope active_scope_ = TextParserScope::Text;
    std::string active_group_ = "All";
    std::vector<const TextParserDefinition*> filtered_parsers_;
    int selected_parser_ = 0;
    int parser_list_top_row_ = 0;

    bool whole_document_ = false;
    std::string repeat_count_ = "1";
    int repeat_cursor_ = 1;
    std::vector<std::string> param_values_ = {"", "", "", ""};
    std::vector<int> param_cursors_ = {0, 0, 0, 0};
    std::string status_ = "Select a text processor.";
    bool status_is_error_ = false;
    std::string report_text_;

    ftxui::Component text_tab_button_;
    ftxui::Component paragraph_tab_button_;
    ftxui::Component code_tab_button_;
    std::vector<ftxui::Component> group_tab_buttons_;
    ftxui::Component processor_grid_component_;
    std::vector<ftxui::Component> param_inputs_;
    ftxui::Component repeat_input_;
    ftxui::Component whole_document_checkbox_;
    ftxui::Component container_;

    static constexpr int kMaxVisibleProcessorCells = 64;
    mutable std::array<ftxui::Box, kMaxVisibleProcessorCells> processor_cell_boxes_{};
    mutable std::array<int, kMaxVisibleProcessorCells> processor_cell_indices_{};
    mutable int processor_cell_count_ = 0;
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
