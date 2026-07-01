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
#include "git_manager.hpp"
#include "modals/assistant_modals.hpp"
#include "modals/help_dialog.hpp"
#include "modals/modal_ai.hpp"
#include "modals/modal_git.hpp"
#include "modals/modal_git_settings.hpp"
#include "modals/modal_recent_files.hpp"
#include "modals/modal_tts.hpp"
#include "modals/modal_view_layout.hpp"
#include "modals/modal_search_files.hpp"
#include "modals/modal_files.hpp"
#include "modals/modal_text_processors.hpp"
#include "menu_bar.hpp"
#include "opened_config.hpp"
#include "recent_files.hpp"
#include "remote/modal_remote_connections.hpp"
#include "remote/modal_remote_files.hpp"
#include "remote/remote_config_store.hpp"
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

    enum class SearchPanelInput {
        Find,
        Replace,
    };

    enum class EditorLayoutMode {
        Single = 0,
        TwoColumns = 1,
        ThreeColumns = 2,
    };

    struct EditorPaneState {
        size_t document_index = 0;
        std::string role = "General";
    };

    // Keep application layer selection type-safe. FTXUI's Container::Tab API
    // requires an int pointer, so active_layer_index_ is only an adapter at
    // that boundary.
    enum class UiLayer {
        Main,
        Help,
        Theme,
        Find,
        GoToLine,
        UnsavedChanges,
        RecentFiles,
        SearchFiles,
        Files,
        TextProcessors,
        RemoteConnections,
        RemoteFiles,
        Git,
        GitSettings,
        Tts,
        ViewLayout,
        AiActions,
        AssistantSettings,
    };

    void SetActiveLayer(UiLayer layer);
    UiLayer ActiveLayer() const;
    bool ActiveModalIsOpen() const;

    void CloseDropdown();
    void OpenDropdown();
    void OpenRecentFilesModal();
    void CloseRecentFilesModal();

    void OpenSearchFilesModal();
    void CloseSearchFilesModal();
    void OpenFilesModal(FilesModalMode mode);
    void CloseFilesModal();
    void OpenTextProcessorsModal();
    void CloseTextProcessorsModal();
    void OpenRemoteConnectionsModal();
    void CloseRemoteConnectionsModal();
    void OpenRemoteFilesModal();
    void CloseRemoteFilesModal();
    void OpenGitModal();
    void CloseGitModal();
    void OpenGitSettingsModal();
    void CloseGitSettingsModal();
    bool OpenGitCompareDocuments(
        const std::string& left_title,
        const std::string& left_content,
        const std::string& right_title,
        const std::string& right_content,
        std::string& error);

    std::vector<FileSearchRoot> CurrentSearchFileRoots() const;
    bool OpenSearchFileMatch(const FileSearchMatch& match, std::string& error);
    bool InsertImportedText(
        const std::filesystem::path& path,
        const std::string& text,
        std::string& error);
    bool ConfirmFilesModalAction(
        FilesModalMode mode,
        const std::filesystem::path& path,
        std::string& error);
    std::vector<std::filesystem::path> FileModalFavoriteDirectories() const;
    bool AddFileModalDirectory(
        const std::filesystem::path& directory,
        std::string& error);
    void CopyFileModalPathText(const std::string& text);
    bool GetTextProcessorTargetText(
        bool whole_document,
        std::string& text,
        std::string& error);
    bool ReplaceTextProcessorTargetText(
        bool whole_document,
        const std::string& text,
        std::string& error);

    std::filesystem::path CurrentSidebarDirectory() const;
    void OpenAboutDialog();
    void OpenHelpDialog();
    void CloseHelpDialog();
    void OpenTtsModal();
    void CloseTtsModal();
    void OpenViewLayoutModal();
    void CloseViewLayoutModal();
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
    std::shared_ptr<EditorComponent> ActiveEditor() const;
    size_t VisibleEditorPaneCount() const;
    std::string EditorLayoutModeLabel(EditorLayoutMode mode) const;
    int EditorLayoutModeIndex() const;
    void SetEditorLayoutMode(EditorLayoutMode mode);
    void SetActiveEditorPane(size_t pane_index);
    void FocusNextEditorPane();
    void FocusPreviousEditorPane();
    void EqualizeEditorPaneWidths();
    void SetEditorPaneRole(size_t pane_index, const std::string& role);
    void AssignDocumentToEditorPane(size_t pane_index, size_t document_index);
    void SplitActiveDocumentToNextPane();
    bool MainViewCanActivateEditorPane() const;
    void EnsureEditorPanesHaveDocuments();
    void AssignDocumentToActivePane(size_t document_index);
    void SyncEditorPaneDocuments();
    ftxui::Element RenderEditorPane(size_t pane_index);
    ViewLayoutSnapshot CurrentViewLayoutSnapshot() const;
    void InitializeWithFiles(const std::vector<std::string>& files_to_open);
    void OpenSidebarFile(const std::filesystem::path& path);
    void SaveCurrentFile();
    void SaveAllOpenedFiles();
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
    ftxui::Element RenderTitleBar();
    ftxui::Element RenderTtsHeaderStrip();
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
    void PasteIntoFindPanelInput();
    void ClearFindPanelFields();
    bool HandleTerminalBracketedPaste(ftxui::Event event);
    std::string FindMatchStatus() const;
    
    // Sub-routers for handling dropdown actions by category
    void HandleFileMenu(int item);
    void HandleEditMenu(int item);
    void HandleOptionsMenu(int item);
    void HandleAiMenu(int item);
    void HandleRemoteMenu(int item);
    void HandleGitMenu(int item);
    
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
    FileManager file_manager_;
    RemoteConfigStore remote_config_store_;
    ftxui::Component text_editor_;
    std::vector<ftxui::Component> editor_pane_components_;
    std::vector<ftxui::Component> editor_pane_renderers_;
    ftxui::Component editor_workspace_single_;
    ftxui::Component editor_workspace_two_;
    ftxui::Component editor_workspace_three_;
    ftxui::Component editor_workspace_container_;
    ftxui::Component sidebar_panel_;
    HelpDialog help_dialog_;
    RecentFilesHistory recent_files_history_;
    RecentFilesModal recent_files_modal_;

    SearchFilesModal search_files_modal_;
    FilesModal files_modal_;
    TextProcessorsModal text_processors_modal_;
    RemoteConnectionsModal remote_connections_modal_;
    RemoteFilesModal remote_files_modal_;
    GitModal git_modal_;
    GitSettingsModal git_settings_modal_;

    CloudTtsPipeline cloud_tts_pipeline_;
    TtsModal tts_modal_;
    ViewLayoutModal view_layout_modal_;
    AiActionsModal ai_actions_modal_;
    AssistantSettingsModal assistant_settings_modal_;
    ThemeDialog theme_dialog_;
    UnsavedChangesDialog unsaved_changes_dialog_;
    std::vector<std::shared_ptr<Document>> open_documents_;
    size_t active_document_index_ = 0;
    std::vector<EditorPaneState> editor_panes_;
    size_t active_editor_pane_index_ = 0;
    EditorLayoutMode editor_layout_mode_ = EditorLayoutMode::Single;
    int editor_workspace_tab_index_ = 0;
    int editor_two_left_width_ = 72;
    int editor_three_left_width_ = 48;
    int editor_three_right_width_ = 48;

    std::string find_query_;
    std::string replace_text_;
    std::string goto_line_input_;
    std::string active_action_ = "Ready";

    UiLayer active_layer_ = UiLayer::Main;
    int active_layer_index_ = static_cast<int>(UiLayer::Main);
    int search_panel_tab_index_ = 0;
    int find_input_cursor_position_ = 0;
    int replace_find_input_cursor_position_ = 0;
    int replace_input_cursor_position_ = 0;
    bool sidebar_has_focus_ = false;
    bool pending_sidebar_chord_ = false;
    bool show_goto_line_bar_ = false;
    bool exit_after_save_as_ = false;
    bool terminal_bracketed_paste_active_ = false;
    SearchMode current_search_mode_ = SearchMode::None;
    SearchPanelInput active_search_panel_input_ = SearchPanelInput::Find;
    std::string terminal_bracketed_paste_buffer_;
    std::shared_ptr<MenuBarComponent> menu_bar_;
    ftxui::Component title_bar_component_;
    ftxui::Component title_bar_open_tts_button_;
    ftxui::Component body_container_;
    ftxui::Component main_container_;
    ftxui::Component root_container_;
    ftxui::Component find_input_;
    ftxui::Component replace_find_input_;
    ftxui::Component replace_input_;
    ftxui::Component find_paste_button_;
    ftxui::Component find_clear_button_;
    ftxui::Component replace_paste_button_;
    ftxui::Component replace_clear_button_;
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
