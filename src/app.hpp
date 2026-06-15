#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "cloud_tts_pipeline.hpp"
#include "config_manager.hpp"
#include "editor_component.hpp"
#include "editor_config.hpp"
#include "file_dialog.hpp"
#include "git_manager.hpp"
#include "help_dialog.hpp"
#include "sidebar_component.hpp"
#include "theme_dialog.hpp"

namespace textlt {

class TextltApp {
public:
    TextltApp();
    explicit TextltApp(const std::vector<std::string>& files_to_open);
    void Run();

private:
    enum class SearchMode {
        None,
        Find,
        Replace,
    };

    void CloseDropdown();
    void OpenDropdown();
    void CloseFileDialog();
    void OpenFileDialog(FilePromptMode mode);
    void OpenAboutDialog();
    void OpenHelpDialog();
    void CloseHelpDialog();
    void OpenThemeDialog();
    void CloseThemeDialog();
    void ShowExitConfirmationDialog();
    void CloseExitConfirmationDialog();
    void RequestExit();
    void SaveAndExit();
    void DiscardAndExit();
    void ActivateTopMenu();
    void SwitchEditorFocus();
    void FocusEditor();
    void FocusSidebar();
    bool SaveFile(const std::string& path, std::string& error);
    bool OpenFile(const std::string& path, std::string& error);
    void InitializeWithFiles(const std::vector<std::string>& files_to_open);
    void OpenSidebarFile(const std::filesystem::path& path);
    void SaveCurrentFile();
    bool ConfirmFileDialog(FilePromptMode mode, const std::string& path, std::string& error);
    void PersistActiveFavoriteCursor();
    void RestoreFavoriteCursor(const std::string& path);
    void QueueCloudTtsDebugFromCursor();
    void PreviewTheme(const std::string& theme_name);
    void SelectTheme(const std::string& theme_name);
    void SaveConfig();
    std::string ActiveDocumentFavoritePath() const;
    void ToggleActiveFavorite();
    void UpdateFileMenuLabels();
    void UpdateOptionsMenuLabels();
    void RunDropdownAction();
    ftxui::Element Render();
    ftxui::Element RenderFindPanel();
    ftxui::Element RenderGoToLinePanel();
    ftxui::Element RenderExitConfirmationDialog();
    bool HandleGlobalEvent(ftxui::Event event);
    int DropdownX() const;
    void OpenFindPanel(bool replace_mode);
    void CloseFindPanel();
    void OpenGoToLinePanel();
    void CloseGoToLinePanel();
    void SubmitGoToLine();
    void RefreshFindMatches();
    void FindNext();
    void FindPrevious();
    void ReplaceNext();
    void ReplaceAll();
    std::string FindMatchStatus() const;
    std::string FileTypeLabel(const std::string& file_path) const;
    
    // Sub-routers for handling dropdown actions by category
    void HandleFileMenu(int item);
    void HandleEditMenu(int item);
    void HandleOptionsMenu(int item);
    
    // Platform-specific clipboard abstraction utilities
    std::string ReadSystemClipboard();
    void WriteSystemClipboard(const std::string& text);

    ConfigManager config_manager_;
    EditorConfig editor_config_;
    std::vector<Theme> themes_;
    Theme current_theme_;
    ftxui::ScreenInteractive screen_;
    GitManager git_manager_;
    ftxui::Component text_editor_;
    ftxui::Component sidebar_panel_;
    FileDialog file_dialog_;
    HelpDialog help_dialog_;
    ThemeDialog theme_dialog_;
    CloudTtsPipeline cloud_tts_pipeline_;

    std::vector<std::string> menu_entries_;
    std::vector<std::vector<std::string>> dropdown_entries_;
    std::vector<std::string> current_dropdown_entries_;
    std::string find_query_;
    std::string replace_text_;
    std::string goto_line_input_;
    std::string active_action_ = "Ready";

    int selected_menu_item_ = 0;
    int active_dropdown_ = -1;
    int focused_layer_ = 0;
    int selected_dropdown_item_ = 0;
    int search_panel_tab_index_ = 0;
    bool sidebar_has_focus_ = false;
    bool show_goto_line_bar_ = false;
    bool exit_confirmation_open_ = false;
    bool exit_after_save_as_ = false;
    SearchMode current_search_mode_ = SearchMode::None;
    ftxui::Box top_menu_box_;

    ftxui::Component top_menu_;
    ftxui::Component dropdown_menu_;
    ftxui::Component body_container_;
    ftxui::Component main_container_;
    ftxui::Component root_container_;
    ftxui::Component find_input_;
    ftxui::Component replace_find_input_;
    ftxui::Component replace_input_;
    ftxui::Component find_next_button_;
    ftxui::Component find_previous_button_;
    ftxui::Component replace_next_button_;
    ftxui::Component replace_all_button_;
    ftxui::Component find_panel_find_container_;
    ftxui::Component find_panel_replace_container_;
    ftxui::Component find_panel_fields_container_;
    ftxui::Component find_panel_filters_container_;
    ftxui::Component find_panel_container_;
    ftxui::Component search_match_case_checkbox_;
    ftxui::Component search_whole_word_checkbox_;
    ftxui::Component goto_line_input_component_;
    ftxui::Component exit_save_button_;
    ftxui::Component exit_discard_button_;
    ftxui::Component exit_cancel_button_;
    ftxui::Component exit_confirmation_container_;
    ftxui::Component renderer_;
    ftxui::Component global_shortcuts_;
};

} // namespace textlt
