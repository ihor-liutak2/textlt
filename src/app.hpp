#pragma once

#include <filesystem>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/app.hpp"
#include "ftxui/dom/elements.hpp"
#include "app_command_registry.hpp"
#include "app_event_dispatcher.hpp"
#include "shortcut_registry.hpp"
#include "shortcut_store.hpp"
#include "cloud_tts_pipeline.hpp"
#include "config_manager.hpp"
#include "context_menu_component.hpp"
#include "editor_component.hpp"
#include "editor_keymap.hpp"
#include "editor_config.hpp"
#include "editor/document_workspace.hpp"
#include "editor/document_file_controller.hpp"
#include "editor/clipboard_controller.hpp"
#include "distraction_mode_controller.hpp"
#include "git_manager.hpp"
#include "modals/assistant_modals.hpp"
#include "modals/help_dialog.hpp"
#include "modals/modal_sidebar_shortcuts.hpp"
#include "modals/modal_keyboard_shortcuts.hpp"
#include "modals/modal_custom_processor_builder.hpp"
#include "modals/modal_ai_settings.hpp"
#include "modals/modal_quick_ai.hpp"
#include "modals/modal_git.hpp"
#include "modals/modal_git_settings.hpp"
#include "modals/modal_connect_settings.hpp"
#include "modals/modal_telegram_settings.hpp"
#include "modals/modal_irc_settings.hpp"
#include "modals/modal_textlt_config.hpp"
#include "modals/modal_recent_files.hpp"
#include "modals/modal_tts.hpp"
#include "modals/modal_view_layout.hpp"
#include "modals/modal_distraction_options.hpp"
#include "modals/modal_theme_colors.hpp"
#include "modals/modal_search_files.hpp"
#include "modals/modal_files.hpp"
#include "modals/modal_text_processors.hpp"
#include "menu_bar.hpp"
#include "remote/modal_remote_connections.hpp"
#include "remote/modal_remote_files.hpp"
#include "remote/remote_config_store.hpp"
#include "sidebar_component.hpp"
#include "open_files_bar.hpp"
#include "top_bar_row.hpp"
#include "bottom_bar_row.hpp"
#include "modals/theme_dialog.hpp"
#include "modals/unsaved_changes_dialog.hpp"
#include "file_manager.hpp"
#include "layout_controller.hpp"
#include "notes/notes_workspace_component.hpp"
#include "actions/actions_workspace_component.hpp"
#include "find_replace/actions_find_target.hpp"
#include "find_replace/editor_find_target.hpp"
#include "find_replace/find_replace_panel.hpp"
#include "find_replace/notes_find_target.hpp"
#include "project_store.hpp"

namespace textlt {

struct InputSelection;

class TextltApp {
    friend class AppEventDispatcher;
    friend class EditorKeymap;
public:
    TextltApp();
    explicit TextltApp(const std::vector<std::string>& files_to_open);
    void Run();

private:
    enum class NavigationPopupMode {
        GoToLine,
        GoToPage,
    };

    using EditorLayoutMode = LayoutController::EditorLayoutMode;

    // Keep application layer selection type-safe. FTXUI's Container::Tab API
    // requires an int pointer, so active_layer_index_ is only an adapter at
    // that boundary.
    enum class UiLayer {
        Main,
        Help,
        KeyboardShortcuts,
        SidebarShortcuts,
        CustomProcessorBuilder,
        Theme,
        ThemeColors,
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
        DistractionOptions,
        QuickAi,
        AiSettings,
        AssistantSettings,
        RenameProject,
        RemoveProject,
        RenameSidebarItem,
        DeleteSidebarItem,
        ConnectSettings,
        TextLTConfig,
        TelegramSettings,
        IrcSettings,
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
    void OpenRenameSidebarItem(const std::filesystem::path& path);
    void SubmitRenameSidebarItem();
    void CloseRenameSidebarItem();
    void RequestDeleteSidebarItem(const std::filesystem::path& path);
    ftxui::Element RenderDeleteSidebarItemPopup();
    bool HandleDeleteSidebarItemEvent(ftxui::Event event);
    void DeleteSidebarItem();
    void OpenTextProcessorsModal(bool quick = false);
    void CloseTextProcessorsModal();
    void OpenRemoteConnectionsModal();
    void CloseRemoteConnectionsModal();
    void OpenRemoteFilesModal();
    void CloseRemoteFilesModal();
    void OpenGitModal();
    void OpenGitBranchesModal();
    void CloseGitModal();
    void OpenGitSettingsModal();
    void CloseGitSettingsModal();
    bool OpenGitCompareDocuments(
        const std::string& left_title,
        const std::string& left_content,
        const std::string& right_title,
        const std::string& right_content,
        std::string& error);
    bool NavigateGitCompare(size_t pane_index, bool next);
    bool IsGitCompareSession(const std::shared_ptr<DocumentSession>& session) const;
    void EndGitCompare();
    void CloseGitCompare();

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
    bool RemoveFileModalDirectory(
        const std::filesystem::path& directory,
        std::string& error);
    bool GetTextProcessorTargetText(
        ProcessorTarget target,
        std::string& text,
        std::string& error);
    bool ReplaceTextProcessorTargetText(
        ProcessorTarget target,
        const std::string& text,
        std::string& error);

    std::filesystem::path CurrentSidebarDirectory() const;
    void OpenAboutDialog();
    void OpenHelpDialog();
    void CloseHelpDialog();
    void OpenKeyboardShortcutsModal();
    void CloseKeyboardShortcutsModal();
    void OpenSidebarShortcutModal();
    void CloseSidebarShortcutModal();
    void OpenCustomProcessorBuilderModal();
    void CloseCustomProcessorBuilderModal();
    void OpenTtsModal();
    void CloseTtsModal();
    void OpenViewLayoutModal();
    void CloseViewLayoutModal();
    void OpenDistractionOptionsModal();
    void CloseDistractionOptionsModal();
    void OpenQuickAiModal();
    void CloseQuickAiModal();
    void OpenAiSettingsModal();
    void CloseAiSettingsModal();
    void OpenAssistantSettingsModal();
    void CloseAssistantSettingsModal();
    void OpenThemeDialog();
    void CloseThemeDialog();
    void OpenThemeColorsModal();
    void CloseThemeColorsModal();
    void ShowUnsavedChangesDialog(const std::string& title);
    void CancelUnsavedChanges();
    void RequestExit();
    void SavePendingChanges();
    void FinishPendingClose();
    void ActivateTopMenu();
    void SwitchEditorFocus();
    void FocusEditor();
    void FocusSidebar();
    bool SaveFile(const std::shared_ptr<DocumentSession>& document,
        const std::filesystem::path& path, std::string& error);
    bool OpenFile(const std::string& path, std::string& error);
    void ActivateOpenSession(size_t index);
    void AddOpenSession(std::shared_ptr<DocumentSession> doc);
    void EnsureOneOpenSession();
    void CloseCurrentFile();
    void CloseAllOpenedFiles();
    void RequestCloseOpenSession(size_t index);
    void CloseOpenSession(const std::shared_ptr<DocumentSession>& document);
    void PersistOpenedSessions();
    void RestoreOpenedSessions();
    std::shared_ptr<DocumentSession> ActiveSessionPtr() const;
    std::shared_ptr<EditorComponent> ActiveEditor() const;
    size_t VisibleEditorPaneCount() const;
    std::string EditorLayoutModeLabel(EditorLayoutMode mode) const;
    int EditorLayoutModeIndex() const;
    void SetEditorLayoutMode(EditorLayoutMode mode);
    void SetActiveEditorPane(size_t pane_index);
    void FocusNextEditorPane();
    void FocusPreviousEditorPane();
    void EqualizeEditorPaneWidths();
    void AssignSessionToEditorPane(size_t pane_index, size_t session_index);
    void SplitActiveSessionToNextPane();
    bool MainViewCanActivateEditorPane() const;
    void BindEditorComponentsToWorkspace();
    ftxui::Element RenderEditorPane(size_t pane_index);
    ViewLayoutSnapshot CurrentViewLayoutSnapshot() const;
    DistractionTopBarState CurrentDistractionTopBarState() const;
    void ApplyDistractionSettings(DistractionModeSettings settings);
    void SetDistractionEnabled(bool enabled);
    void InitializeWithFiles(const std::vector<std::string>& files_to_open);
    void OpenSidebarFile(const std::filesystem::path& path);
    void SaveCurrentFile();
    void SaveAllOpenedFiles();
    void PersistActiveFavoriteCursor();
    void RestoreFavoriteCursor(const std::string& path);
    void QueueTtsBookPreparationFromCursor(bool force_rebuild = false);
    void SetTtsHeaderActiveButton(TtsHeaderButton button);
    void PreviewTheme(const std::string& theme_name);
    void SelectTheme(const std::string& theme_name);
    void SaveConfig();
    std::string ActiveSessionFavoritePath() const;
    void ToggleActiveFavorite();
    void AddActiveFavoriteFromSidebar();
    void UpdateFileMenuLabels();
    void UpdateOptionsMenuLabels();
    void UpdateNotesMenuLabels();
    void InitializeCommands();
    void InitializeMenuShortcuts();
    void InitializeTextShortcuts();
    bool SaveShortcutOverrides(std::string& error);
    bool RunCommand(const std::string& command_id);
    bool RunMenuShortcut(ftxui::Event event);
    bool RunTextShortcut(ftxui::Event event);
    void RefreshProjectSidebar();
    void RunDropdownAction(int menu_index, int item_index);
    ftxui::Element Render();
    ftxui::Element RenderGoToLinePanel();
    ftxui::Element RenderRenameProjectPanel();
    BottomBarRowState CurrentBottomBarRowState();
    bool HandleGlobalEvent(ftxui::Event event);
    void DeliverClipboardText(std::optional<std::string> text);
    int DropdownX() const;
    void OpenGoToLinePanel();
    void OpenDistractionPagePanel();
    void CloseGoToLinePanel();
    void SubmitGoToLine();
    void ToggleFileExplorer();
    void ShowOpenFileList();
    void ShowProjectSidebar();
    void ShowProjectsSidebar();
    void ShowFavoritesSidebar();
    void ToggleNotesWorkspace();
    void ShowNotesWorkspace();
    void ShowActionsWorkspace();
    void ShowConnectWorkspace();
    void ShareNoteViaConnect();
    void ShareActionViaConnect();
    void UpdateWorkspaceMenus();
    void OpenConnectSettingsModal(ConnectSettingsTab tab);
    void CloseConnectSettingsModal();
    void OpenTelegramSettingsModal();
    void CloseTelegramSettingsModal();
    void OpenIrcSettingsModal();
    void CloseIrcSettingsModal();
    void OpenTextLTConfigModal();
    void CloseTextLTConfigModal();
    void ShowDocumentsWorkspace();
    void RefreshActionsSidebar();
    void SyncActionsProjectContext();
    void ToggleOpenFilesAndFolder();
    void CopySidebarSelectedPath();
    void InitializeProjects();
    void SyncNotesProjectContext();
    std::vector<SidebarPanel::ProjectEntry> SidebarProjects() const;
    bool RegisterProject(const std::filesystem::path& root, std::string& error);
    bool ActivateProject(const std::string& id);
    void RequestRemoveProject(const std::string& id);
    ftxui::Element RenderRemoveProjectPopup();
    bool HandleRemoveProjectEvent(ftxui::Event event);
    void RemoveProject(const std::string& id);
    bool FinishProjectGroupDetachments(std::string& error);
    void OpenRenameProject(const std::string& id);
    void SubmitRenameProject();
    void CloseRenameProject();
    bool UpdateProjectGroupNames(const ProjectRecord& project, std::string& error);
    void OpenProjectRemote(std::string id);
    bool OpenWorkspaceFolder(const std::filesystem::path& root, std::string& error);
    const ProjectRecord* ActiveWorkspaceProject() const;
    void AddCurrentFolderAsProject();
    void AssignProjectConnection(const std::string& id);
    void EditProjectConnection(const std::string& id);
    void UnlinkProjectConnection(const std::string& id);
    bool SetProjectConnection(
        const std::string& project_id,
        const std::string& connection_id,
        std::string& error);
    bool CanDeleteRemoteConnection(const std::string& connection_id, std::string& error) const;
    
    // Built-in command actions. UI surfaces and shortcuts call these through command IDs.
    void CommandFileNew();
    void CommandFileOpen();
    void CommandFileSaveAs();
    void CommandFileImport();
    void CommandFileRecent();
    void CommandFileClose();
    void CommandFileCloseAll();
    void CommandFileSave();
    void CommandFileSaveAll();
    void CommandFileToggleFavorite();

    void CommandEditUndo();
    void CommandEditRedo();
    void CommandEditSelectAll();
    void CommandEditCut();
    void CommandEditCopy();
    void CommandEditPaste();
    void CommandEditToggleComment();
    void CommandEditChangeCase(transform::CaseMode mode);
    void CommandEditorIndentRight();
    void CommandFindSelection();
    void CommandCopyFilePath();
    bool OpenEditorContextMenu(int x, int y);
    void CommandEditConvertIndents4To2();
    void CommandEditConvertIndents2To4();
    void CommandEditFind();
    void CommandEditReplace();
    void OpenFindPanel(bool replace_tab, std::optional<std::string> query = std::nullopt);
    void CloseFindPanel();
    void SyncFindPanelTarget();
    void RestoreNotesSidebarForFindPanel();
    void CommandSearchFiles();
    void CommandTextProcessors();
    void CommandCustomProcessorBuilder();

    void CommandViewToggleLineNumbers();
    void CommandSidebarToggleFileExplorer();
    void CommandEditorToggleSmartWordWrap();
    void CommandEditorToggleSyntaxHighlighting();
    void CommandEditorToggleAutoPairing();
    void CommandEditorToggleAutoIndent();
    void CommandEditorToggleTabSize();
    void CommandEditorConvertTabsToSpaces();
    void CommandThemeOpen();
    void CommandViewLayoutOpen();
    void CommandDistractionOpenOptions();
    void CommandDistractionEnter();
    void CommandDistractionExit();
    void CommandDistractionToggle();
    void CommandDistractionNextPage();
    void CommandDistractionPreviousPage();
    void CommandDistractionGoToPage();

    void CommandNotesToggle();
    void CommandNotesNew();
    void CommandNotesImportMarkdown();
    void CommandNotesEditSelected();
    void CommandNotesDeleteSelected();
    void CommandNotesRestoreSelected();
    void CommandNotesEmptyTrash();
    void CommandNotesNewGroup();
    void CommandNotesRenameGroup();
    void CommandNotesDeleteGroup();
    void CommandNotesOpenProjectNotes();
    void CommandNotesGroupToGeneral();
    void CommandNotesRelinkGroup();
    void CommandNotesHeading1();
    void CommandNotesHeading2();
    void CommandNotesHeading3();
    void CommandNotesParagraph();
    void CommandNotesBulletList();
    void CommandNotesNumberedList();
    void CommandNotesChecklist();
    void CommandActionsNewGroup();
    void CommandActionsRenameGroup();
    void CommandActionsDeleteGroup();
    void CommandActionsMergeGroup();
    void CommandActionsNewPage();
    void CommandActionsRenamePage();
    void CommandActionsDeletePage();
    void CommandActionsMovePage(int delta);
    void CommandActionsAdd();
    void CommandActionsImport();
    void CommandActionsEdit();
    void CommandActionsDelete();
    void CommandActionsMove(int delta);
    void CommandActionsCopy();
    void CommandActionsCopyAll();

    void CommandTtsPlay();
    void CommandTtsPause();
    void CommandTtsStop();
    void CommandTtsNext();

    AppCommandRegistry command_registry_;
    ShortcutStore shortcut_store_;
    ShortcutRegistry shortcut_registry_;
    EditorKeymap editor_keymap_;
    ConfigManager config_manager_;
    EditorConfig editor_config_;
    DistractionModeController distraction_controller_;
    std::vector<Theme> themes_;
    Theme current_theme_;
    ContextMenuComponent editor_context_menu_;
    ftxui::App screen_;
    AppEventDispatcher app_event_dispatcher_;
    GitManager git_manager_;
    FileManager file_manager_;
    DocumentWorkspace document_workspace_;
    LayoutController layout_controller_;
    DocumentFileController document_file_controller_;
    ClipboardController clipboard_controller_;
    RemoteConfigStore remote_config_store_;
    ProjectStore project_store_;
    ftxui::Component text_editor_;
    std::vector<ftxui::Component> editor_pane_components_;
    struct GitCompareState {
        struct Pane {
            std::weak_ptr<DocumentSession> session;
            EditorCursorState cursor;
        };
        EditorLayoutMode previous_layout;
        size_t active_pane;
        std::weak_ptr<DocumentSession> active_session;
        std::array<Pane, 2> previous_panes;
        std::array<std::weak_ptr<DocumentSession>, 2> compare_sessions;
    };
    std::optional<GitCompareState> git_compare_state_;
    std::array<ftxui::Box, 3> git_compare_prev_boxes_{};
    std::array<ftxui::Box, 3> git_compare_next_boxes_{};
    std::vector<ftxui::Component> editor_pane_renderers_;
    ftxui::Component editor_workspace_single_;
    ftxui::Component editor_workspace_two_;
    ftxui::Component editor_workspace_three_;
    ftxui::Component editor_workspace_container_;
    ftxui::Component sidebar_panel_;
    OpenFilesBar open_files_bar_;
    ftxui::Component notes_workspace_component_;
    ftxui::Component actions_workspace_component_;
    std::shared_ptr<connect_ui::ConnectWorkspace> connect_workspace_;
    int connect_body_index_ = 0;
    std::unique_ptr<EditorFindTarget> editor_find_target_;
    std::unique_ptr<NotesFindTarget> notes_find_target_;
    std::unique_ptr<ActionsFindTarget> actions_find_target_;
    std::unique_ptr<FindReplacePanel> find_replace_panel_;
    bool find_panel_open_ = false;
    bool find_panel_focus_ = false;
    bool find_panel_notes_sidebar_visible_ = false;
    bool find_panel_hid_notes_sidebar_ = false;
    HelpDialog help_dialog_;
    KeyboardShortcutsModal keyboard_shortcuts_modal_;
    SidebarShortcutModal sidebar_shortcuts_modal_;
    CustomProcessorBuilderModal custom_processor_builder_modal_;
    RecentFilesModal recent_files_modal_;

    SearchFilesModal search_files_modal_;
    FilesModal files_modal_;
    TextProcessorsModal text_processors_modal_;
    RemoteConnectionsModal remote_connections_modal_;
    RemoteFilesModal remote_files_modal_;
    GitModal git_modal_;
    GitSettingsModal git_settings_modal_;
    ConnectSettingsModal connect_settings_modal_;
    TelegramSettingsModal telegram_settings_modal_;
    IrcSettingsModal irc_settings_modal_;
    TextLTConfigModal textlt_config_modal_;

    CloudTtsPipeline cloud_tts_pipeline_;
    TtsModal tts_modal_;
    ViewLayoutModal view_layout_modal_;
    DistractionOptionsModal distraction_options_modal_;
    QuickAiModal quick_ai_modal_;
    AiSettingsModal ai_settings_modal_;
    AssistantSettingsModal assistant_settings_modal_;
    ThemeColorsModal theme_colors_modal_;
    ThemeDialog theme_dialog_;
    UnsavedChangesDialog unsaved_changes_dialog_;
    std::string goto_line_input_;
    std::string rename_project_id_;
    std::string remove_project_id_;
    std::string project_name_;
    std::string rename_project_error_;
    std::filesystem::path sidebar_item_operation_path_;
    std::string sidebar_item_name_;
    std::string sidebar_item_operation_error_;
    std::shared_ptr<InputSelection> sidebar_item_name_selection_;
    std::string active_action_ = "Ready";
    TtsHeaderButton tts_header_active_button_ = TtsHeaderButton::Open;

    UiLayer active_layer_ = UiLayer::Main;
    int active_layer_index_ = static_cast<int>(UiLayer::Main);
    int goto_line_input_cursor_position_ = 0;
    bool sidebar_has_focus_ = false;
    bool show_goto_line_bar_ = false;
    NavigationPopupMode navigation_popup_mode_ = NavigationPopupMode::GoToLine;
    bool exit_after_save_as_ = false;
    std::shared_ptr<DocumentSession> pending_close_session_;
    WorkspaceMode workspace_mode_ = WorkspaceMode::Documents;
    int workspace_mode_index_ = static_cast<int>(WorkspaceMode::Documents);
    std::string workspace_project_id_;
    std::shared_ptr<MenuBarComponent> menu_bar_;
    ftxui::Component top_bar_row_;
    ftxui::Component bottom_bar_row_;
    ftxui::Component body_container_;
    ftxui::Component main_container_;
    ftxui::Component root_container_;
    ftxui::Component goto_line_input_component_;
    ftxui::Component goto_line_go_button_;
    ftxui::Component goto_line_cancel_button_;
    ftxui::Component goto_line_popup_container_;
    std::shared_ptr<InputSelection> project_name_selection_;
    ftxui::Component project_name_input_;
    ftxui::Component rename_project_button_;
    ftxui::Component cancel_rename_project_button_;
    ftxui::Component rename_project_popup_;
    ftxui::Component remove_project_popup_;
    ftxui::Component sidebar_item_name_input_;
    ftxui::Component rename_sidebar_item_button_;
    ftxui::Component cancel_rename_sidebar_item_button_;
    ftxui::Component rename_sidebar_item_popup_;
    ftxui::Component delete_sidebar_item_popup_;
    ftxui::Box remove_project_cancel_box_;
    ftxui::Box remove_project_delete_box_;
    ftxui::Box delete_sidebar_item_cancel_box_;
    ftxui::Box delete_sidebar_item_confirm_box_;
    ftxui::Component renderer_;
    ftxui::Component global_shortcuts_;
};

} // namespace textlt
