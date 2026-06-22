#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "cloud_tts_pipeline.hpp"
#include "config_manager.hpp"
#include "editor_component.hpp"
#include "editor_config.hpp"
#include "modals/file_dialog.hpp"
#include "git_manager.hpp"
#include "modals/assistant_modals.hpp"
#include "modals/help_dialog.hpp"
#include "modals/modal_ai.hpp"
#include "modals/modal_tts.hpp"
#include "menu_bar.hpp"
#include "opened_config.hpp"
#include "modals/path_operation_dialog.hpp"
#include "sidebar_component.hpp"
#include "modals/theme_dialog.hpp"
#include "modals/unsaved_changes_dialog.hpp"
#include "file_manager.hpp"

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
    void ClosePathOperationDialog();
    void OpenPathOperationDialog(PathOperationMode mode);
    std::filesystem::path CurrentSidebarDirectory() const;
    std::string SelectedSidebarFileName() const;
    std::string SelectedSidebarPathName() const;
    void OpenAboutDialog();
    void OpenHelpDialog();
    void CloseHelpDialog();
    void OpenTtsModal();
    void CloseTtsModal();
    void OpenAiActionsModal();
    void CloseAiActionsModal();
    void OpenAssistantSettingsModal();
    void CloseAssistantSettingsModal();
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
    bool CreateFolderInCurrentDirectory(const std::string& name, std::string& error);
    bool DeleteFolderInCurrentDirectory(const std::string& name, std::string& error);
    bool DeleteFileInCurrentDirectory(const std::string& name, std::string& error);
    bool ConfirmPathOperation(
        PathOperationMode mode,
        const std::string& from,
        const std::string& to,
        std::string& error);
    void ActivateOpenDocument(size_t index);
    int FindOpenDocument(const std::filesystem::path& path) const;
    void AddOpenDocument(std::shared_ptr<Document> doc);
    bool IsMemoryOnlyDocument(const std::shared_ptr<Document>& doc) const;
    void EnsureOneOpenDocument();
    void RemoveOpenDocument(size_t index);
    void CloseCurrentFile();
    void CloseAllOpenedFiles();
    void PersistOpenedDocuments();
    void RestoreOpenedDocuments();
    void OpenRestoredDocument(const OpenedFileState& entry);
    void RefreshOpenedDocumentsSidebar();
    std::shared_ptr<Document> ActiveDocument() const;
    void InitializeWithFiles(const std::vector<std::string>& files_to_open);
    void OpenSidebarFile(const std::filesystem::path& path);
    void SaveCurrentFile();
    void SaveAllOpenedFiles();
    bool ConfirmFileDialog(FilePromptMode mode, const std::string& path, std::string& error);
    void PersistActiveFavoriteCursor();
    void RestoreFavoriteCursor(const std::string& path);
    void QueueTtsBookPreparationFromCursor();
    void PreviewTheme(const std::string& theme_name);
    void SelectTheme(const std::string& theme_name);
    void SaveConfig();
    std::string ActiveDocumentFavoritePath() const;
    void ToggleActiveFavorite();
    void UpdateFileMenuLabels();
    void UpdateOptionsMenuLabels();
    void RefreshProjectSidebar();
    void RunDropdownAction(int menu_index, int item_index);
    ftxui::Element Render();
    ftxui::Element RenderFindPanel();
    ftxui::Element RenderGoToLinePanel();
    bool HandleGlobalEvent(ftxui::Event event);
    int DropdownX() const;
    void OpenFindPanel(bool replace_mode);
    void CloseFindPanel();
    void OpenGoToLinePanel();
    void CloseGoToLinePanel();
    void SubmitGoToLine();
    void ToggleFileExplorer();
    void HandleCtrlBFileExplorer();
    void ShowOpenedFilesSidebar();
    void ToggleSidebarOpenedProject();
    void RefreshFindMatches();
    void FindNext();
    void FindPrevious();
    void ReplaceNext();
    void ReplaceAll();
    std::string FindMatchStatus() const;
    std::vector<std::string> CurrentProjectPathCandidates() const;
    bool ResolveProjectRelativePath(
        const std::string& path,
        std::filesystem::path* resolved,
        std::string& error) const;
    void UpdateOpenDocumentPathsAfterMove(
        const std::filesystem::path& from,
        const std::filesystem::path& to);
    
    // Sub-routers for handling dropdown actions by category
    void HandleFileMenu(int item);
    void HandleEditMenu(int item);
    void HandleOptionsMenu(int item);
    void HandleAiMenu(int item);
    
    // Platform-specific clipboard abstraction utilities
    std::string ReadSystemClipboard();
    void WriteSystemClipboard(const std::string& text);

    ConfigManager config_manager_;
    EditorConfig editor_config_;
    OpenedConfigStore opened_config_store_;
    std::vector<Theme> themes_;
    Theme current_theme_;
    ftxui::ScreenInteractive screen_;
    GitManager git_manager_;
    ftxui::Component text_editor_;
    ftxui::Component sidebar_panel_;
    FileDialog file_dialog_;
    PathOperationDialog path_operation_dialog_;
    HelpDialog help_dialog_;
    CloudTtsPipeline cloud_tts_pipeline_;
    TtsModal tts_modal_;
    AiActionsModal ai_actions_modal_;
    AssistantSettingsModal assistant_settings_modal_;
    ThemeDialog theme_dialog_;
    UnsavedChangesDialog unsaved_changes_dialog_;
    FileManager file_manager_;
    std::vector<std::shared_ptr<Document>> open_documents_;
    size_t active_document_index_ = 0;

    std::string find_query_;
    std::string replace_text_;
    std::string goto_line_input_;
    std::string active_action_ = "Ready";

    int focused_layer_ = 0;
    int search_panel_tab_index_ = 0;
    bool sidebar_has_focus_ = false;
    bool pending_sidebar_chord_ = false;
    bool show_goto_line_bar_ = false;
    bool exit_after_save_as_ = false;
    SearchMode current_search_mode_ = SearchMode::None;
    std::shared_ptr<MenuBarComponent> menu_bar_;
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
    ftxui::Component renderer_;
    ftxui::Component global_shortcuts_;
};

} // namespace textlt
