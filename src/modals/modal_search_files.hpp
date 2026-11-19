#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"

#include "modal_interface.hpp"
#include "modal_window.hpp"
#include "json_utils.hpp"
#include "search_in_files.hpp"
#include "theme.hpp"

namespace textlt {

class SearchFilesModalContent : public IModalContent {
public:
    using RootProvider = std::function<std::vector<FileSearchRoot>()>;
    using OpenMatchCallback = std::function<bool(const FileSearchMatch& match, std::string& error)>;
    using ReadClipboardCallback = std::function<std::string()>;
    using WriteClipboardCallback = std::function<void(const std::string& text)>;
    using CloseCallback = std::function<void()>;

    SearchFilesModalContent(
        const Theme* theme,
        RootProvider root_provider,
        OpenMatchCallback on_open,
        ReadClipboardCallback read_clipboard,
        WriteClipboardCallback write_clipboard,
        CloseCallback on_close);

    ftxui::Element Render() override;
    ftxui::Component GetMainComponent() override { return container_; }
    std::string GetTitle() override { return "Search in Files"; }
    ftxui::Element RenderTitle() override;
    ModalSizePreference GetModalSizePreference() const override { return {118, 34}; }
    ModalFrameStyle GetModalFrameStyle() const override {
        return ModalFrameStyle::TitleInBorder;
    }
    bool HasCustomFooter() const override { return true; }
    int GetCustomFooterHeight() const override { return 3; }
    ftxui::Element RenderCustomFooter() override;

    void Open();
    void Close();
    void SetTheme(const Theme* theme) { theme_ = theme; }

    bool HandleEvent(ftxui::Event event);

private:
    struct DirectoryChoice {
        FileSearchRoot root;
        bool selected = false;
    };

    ftxui::Component MakeTextButton(
        std::string label,
        std::function<void()> on_click);

    ftxui::Component MakeTabButton(
        std::string label,
        int tab_index);
    ftxui::Component MakeFooterButtons(ftxui::Components buttons);
    void SelectTab(int tab_index);

    void BuildDirectoryChoices();
    void AddDirectoryChoice(
        const FileSearchRoot& root,
        bool selected,
        std::vector<std::string>* seen_paths);

    void RefreshDirectoryLabels();
    void ToggleSelectedDirectory();
    void SelectAllDirectories();
    void ClearDirectorySelection();
    bool HandleDirectoryMouseEvent(ftxui::Event event);

    std::vector<FileSearchRoot> SelectedRoots() const;

    void UseMaskSet(size_t index);
    void ExecuteSearch(FileSearchTarget target = FileSearchTarget::Content);
    void OpenSelectedMatch();
    void PasteSearchQuery();
    void ClearSearchQuery();
    void CopyResultText();
    void CopyResultPaths();

    std::filesystem::path CurrentRootPath() const;
    void RestoreSelectedDirectoriesForCurrentRoot();
    void SaveSelectedDirectoriesForCurrentRoot();
    void SelectMaskByValue(const std::string& value);

    void MoveResultSelection(int delta);
    void ClampResultSelection();

    void RebuildResultLabels();
    bool HandleResultsMouseEvent(ftxui::Event event);
    ftxui::Element RenderSelectedResultPreview() const;

    ftxui::Element RenderHeaderRow();
    ftxui::Element RenderSearchTab();
    ftxui::Element RenderResultsTab();

    ftxui::Element RenderDirectoryList() const;
    ftxui::Element RenderResultList();
    ftxui::Element RenderContextLine(const FileSearchContextLine& line) const;

    std::string StatusText() const;
    std::string FooterSummaryText() const;
    std::string DirectorySummaryText() const;
    std::string FormatLocation(const FileSearchMatch& match) const;
    std::string FormatLineNumber(size_t line_number) const;
    std::string TrimForDisplay(const std::string& text, size_t max_size) const;

    const Theme* theme_ = nullptr;
    RootProvider root_provider_;
    OpenMatchCallback on_open_;
    ReadClipboardCallback read_clipboard_;
    WriteClipboardCallback write_clipboard_;
    CloseCallback on_close_;

    FileSearchEngine engine_;
    FileSearchSummary summary_;

    int selected_tab_ = 0;

    std::vector<FileSearchMaskSet> mask_sets_;
    size_t selected_mask_set_ = 0;

    std::filesystem::path current_root_path_;
    std::vector<DirectoryChoice> directories_;
    std::vector<std::string> directory_labels_;
    int selected_directory_ = 0;

    int query_cursor_position_ = 0;
    std::string query_;
    std::string masks_;
    size_t context_before_ = 0;
    size_t context_after_ = 0;

    std::string status_;

    int selected_result_ = 0;
    std::vector<std::string> result_labels_;
    ftxui::Component result_menu_;
    ftxui::Component result_list_component_;
    ftxui::Box result_list_box_;
    std::vector<ftxui::Box> result_row_boxes_;
    
    std::chrono::steady_clock::time_point last_result_click_time_{};
    int last_clicked_result_ = -1;

    std::chrono::steady_clock::time_point last_directory_click_time_{};
    int last_clicked_directory_ = -1;

    std::chrono::steady_clock::time_point last_query_click_time_{};

    ftxui::Component search_tab_button_;
    ftxui::Component results_tab_button_;
    ftxui::Component filters_tab_button_;
    ftxui::Component tab_buttons_;

    ftxui::Component query_input_;
    ftxui::Component search_paste_button_;
    ftxui::Component search_clear_button_;
    ftxui::Component mask_picker_button_;
    std::vector<std::string> filter_labels_;
    int selected_filter_ = 0;
    ftxui::Box filter_list_box_;
    std::vector<ftxui::Box> filter_row_boxes_;
    std::chrono::steady_clock::time_point last_filter_click_time_{};
    int last_clicked_filter_ = -1;

    std::string filter_name_input_;
    std::string filter_value_input_;

    ftxui::Component filter_menu_;
    ftxui::Component filter_list_component_;
    ftxui::Component filter_name_input_component_;
    ftxui::Component filter_value_input_component_;

    ftxui::Component context_before_input_component_;
    ftxui::Component context_after_input_component_;

    ftxui::Component directory_menu_;
    ftxui::Component directory_list_component_;
    ftxui::Component toggle_directory_button_;
    ftxui::Component all_directories_button_;
    ftxui::Component none_directories_button_;

    std::array<ftxui::Component, 3> tab_footer_buttons_;

    ftxui::Component search_tab_container_;
    ftxui::Component results_tab_container_;
    ftxui::Component filters_tab_container_;
    ftxui::Component tab_body_container_;
    ftxui::Component container_;

    std::filesystem::path FilterConfigPath() const;

    Json LoadFilterConfig() const;
    bool SaveFilterConfig(const Json& root) const;
    void LoadFilters();
    void SaveFilters();

    void RebuildFilterLabels();
    void SyncFilterInputsFromSelection();
    void UpdateSelectedFilterFromInputs();

    void ApplySelectedFilter();
    bool HandleFilterMouseEvent(ftxui::Event event);
    void AddFilter();
    void DeleteFilter();

    ftxui::Element RenderFiltersTab();
};

class SearchFilesModal {
public:
    using RootProvider = SearchFilesModalContent::RootProvider;
    using OpenMatchCallback = SearchFilesModalContent::OpenMatchCallback;

    SearchFilesModal(
        const Theme* theme,
        RootProvider root_provider,
        OpenMatchCallback on_open,
        SearchFilesModalContent::ReadClipboardCallback read_clipboard,
        SearchFilesModalContent::WriteClipboardCallback write_clipboard);

    ftxui::Component View() const;

    void Open();
    void Close();
    bool IsOpen() const;
    bool OnEvent(ftxui::Event event);

private:
    bool open_ = false;
    const Theme* theme_ = nullptr;

    std::shared_ptr<SearchFilesModalContent> content_;
    std::shared_ptr<ModalWindow> modal_;
};

} // namespace textlt
