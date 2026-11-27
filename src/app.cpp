#include "app.hpp"

#include "connect_workspace.hpp"
#include "connect_content.hpp"

#include "path_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "app_resources.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/mouse.hpp"
#include "theme.hpp"
#include "ui_button.hpp"
#include "ui_selectable_input.hpp"
#include "file_manager.hpp"
#include "editor/document_session.hpp"

namespace textlt {

namespace {

Theme ResolveConfiguredTheme(
    const std::vector<Theme>& themes,
    const EditorConfig& config) {
    Theme theme = FindThemeByName(themes, config.active_theme_name);
    const auto overrides = config.theme_color_overrides.find(theme.name);
    if (overrides != config.theme_color_overrides.end()) {
        ApplyThemeColorOverrides(theme, overrides->second);
    }
    return theme;
}

class BracketedPasteModeGuard {
public:
    BracketedPasteModeGuard() {
        ClipboardController::SetBracketedPasteMode(true);
    }

    ~BracketedPasteModeGuard() {
        ClipboardController::SetBracketedPasteMode(false);
    }
};

ftxui::ButtonOption MakePopupFlatButtonOption(
    std::string label,
    std::function<void()> on_click,
    const Theme* theme,
    ButtonRole role = ButtonRole::Default) {
    ButtonSpec base_spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        ButtonVariant::Minimal,
        ButtonSize::Compact);

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base_spec);
    option.on_click = std::move(on_click);
    option.transform = [theme, base_spec = std::move(base_spec)](const ftxui::EntryState& state) {
        const Theme fallback_theme;
        const Theme& resolved_theme = theme ? *theme : fallback_theme;
        return RenderModalFlatButton(resolved_theme, base_spec, state.focused || state.active);
    };
    return option;
}

} // namespace

TextltApp::TextltApp()
    : config_manager_("config.json"),
      editor_config_(config_manager_.Load()),
      distraction_controller_(editor_config_.distraction_mode),
      themes_(LoadThemesFromConfiguredLocations()),
      current_theme_(ResolveConfiguredTheme(themes_, editor_config_)),
      editor_context_menu_(current_theme_, [this](const std::string& command) { RunCommand(command); }),
      screen_(ftxui::App::Fullscreen()),
      app_event_dispatcher_(*this),
      file_manager_(),
      document_workspace_(),
      layout_controller_(document_workspace_, &distraction_controller_),
      document_file_controller_(file_manager_, document_workspace_),
      clipboard_controller_([this] { screen_.PostEvent(ftxui::Event::Custom); }),
      remote_config_store_(),
      project_store_(),
      text_editor_(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_)),
      sidebar_panel_(ftxui::Make<SidebarPanel>(
          [this](const std::filesystem::path& path) { OpenSidebarFile(path); },
          &current_theme_,
          &git_manager_,
          [this] { return document_file_controller_.FavoriteFilePaths(); },
          [this](const std::filesystem::path& path) {
              document_file_controller_.RemoveFavorite(path);
              UpdateFileMenuLabels();
              active_action_ = "Removed favorite " + Utf8FromPath(path);
              screen_.PostEvent(ftxui::Event::Custom);
          },
          [this] { AddActiveFavoriteFromSidebar(); },
          [this] { OpenFilesModal(FilesModalMode::Open); },
          [this](const std::filesystem::path& path) {
              if (path.empty()) {
                  active_action_ = "No Project item selected to copy";
              } else {
                  const std::string path_text = FileManager::PathToUtf8(path);
                  active_action_ = clipboard_controller_.WriteText(path_text)
                      ? "Copied path: " + path_text
                      : "Could not copy path to the system clipboard";
              }
              screen_.PostEvent(ftxui::Event::Custom);
          },
          [this](const std::string& command_id) {
              return shortcut_registry_.EffectiveShortcut(ShortcutContext::Menu, command_id);
          },
          [this](const std::string& id) {
              if (actions_workspace_component_)
                  std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->SelectGroup(id);
          },
          [this](SidebarPanel::ActionCommand command) {
              switch (command) {
                  case SidebarPanel::ActionCommand::NewGroup:
                      CommandActionsNewGroup();
                      break;
                  case SidebarPanel::ActionCommand::RenameGroup:
                      CommandActionsRenameGroup();
                      break;
                  case SidebarPanel::ActionCommand::DeleteGroup:
                      CommandActionsDeleteGroup();
                      break;
                  case SidebarPanel::ActionCommand::MergeGroup:
                      CommandActionsMergeGroup();
                      break;
                  case SidebarPanel::ActionCommand::NewPage:
                      CommandActionsNewPage();
                      break;
                  case SidebarPanel::ActionCommand::RenamePage:
                      CommandActionsRenamePage();
                      break;
                  case SidebarPanel::ActionCommand::DeletePage:
                      CommandActionsDeletePage();
                      break;
                  case SidebarPanel::ActionCommand::AddAction:
                      CommandActionsAdd();
                      break;
                  case SidebarPanel::ActionCommand::EditAction:
                      CommandActionsEdit();
                      break;
                  case SidebarPanel::ActionCommand::DeleteAction:
                      CommandActionsDelete();
                      break;
                  case SidebarPanel::ActionCommand::MoveActionUp:
                      CommandActionsMove(-1);
                      break;
                  case SidebarPanel::ActionCommand::MoveActionDown:
                      CommandActionsMove(1);
                      break;
              }
          },
          [this] { return SidebarProjects(); },
          [this](const std::string& id) { ActivateProject(id); },
          [this](const std::filesystem::path&) { OpenFilesModal(FilesModalMode::SelectDirectory); },
          [this](const std::string& id) { OpenRenameProject(id); },
          [this](const std::string& id) { RequestRemoveProject(id); },
          [this](const std::string& id) { OpenProjectRemote(id); },
          [this](const std::string& id) { AssignProjectConnection(id); },
          [this](const std::string& id) { EditProjectConnection(id); },
          [this](const std::string& id) { UnlinkProjectConnection(id); })),
      open_files_bar_(document_workspace_, current_theme_,
          [this](size_t index) {
              ActivateOpenSession(index);
              FocusEditor();
          },
          [this](size_t index) { RequestCloseOpenSession(index); }),
      help_dialog_(&current_theme_),
      keyboard_shortcuts_modal_(
          &current_theme_,
          &shortcut_registry_,
          &command_registry_,
          [this](std::string& error) { return SaveShortcutOverrides(error); }),
      sidebar_shortcuts_modal_(
          &current_theme_,
          [this](const std::string& command_id) { RunCommand(command_id); },
          [this] { CloseSidebarShortcutModal(); }),
      custom_processor_builder_modal_(
          &current_theme_,
          [this](ClipboardController::PasteCallback deliver) { clipboard_controller_.ReadText(std::move(deliver)); },
          [this](const std::string& text) { return clipboard_controller_.WriteText(text); }),
      recent_files_modal_(
          &current_theme_,
          &document_file_controller_,
          [this](const std::filesystem::path& path, std::string& error) {
              const bool opened = OpenFile(Utf8FromPath(path), error);
              if (opened) {
                  FocusEditor();
                  screen_.PostEvent(ftxui::Event::Custom);
              }
              return opened;
          }),
          search_files_modal_(
              &current_theme_,
              [this] {
                  return CurrentSearchFileRoots();
              },
              [this](const FileSearchMatch& match, std::string& error) {
                  return OpenSearchFileMatch(match, error);
              },
              [this](ClipboardController::PasteCallback deliver) {
                  clipboard_controller_.ReadText(std::move(deliver));
              },
              [this](const std::string& text) {
                  return clipboard_controller_.WriteText(text);
              }),
      files_modal_(
          &current_theme_,
          &file_manager_,
          [this] {
              return CurrentSidebarDirectory();
          },
          [this] {
              return FileModalFavoriteDirectories();
          },
          [this](const std::filesystem::path& directory, std::string& error) {
              return AddFileModalDirectory(directory, error);
          },
          [this](const std::filesystem::path& directory, std::string& error) {
              return RemoveFileModalDirectory(directory, error);
          },
          [this](const std::string& text) {
              return clipboard_controller_.WriteText(text);
          },
          [this](FilesModalMode mode, const std::filesystem::path& path, std::string& error) {
              return ConfirmFilesModalAction(mode, path, error);
          },
          [this] {
              RefreshProjectSidebar();
              git_manager_.Invalidate();
          }),
      text_processors_modal_(
          &current_theme_,
          [this](
              ProcessorTarget target,
              std::string& text,
              std::string& error) {
              return GetTextProcessorTargetText(target, text, error);
          },
          [this](
              ProcessorTarget target,
              const std::string& text,
              std::string& error) {
              return ReplaceTextProcessorTargetText(target, text, error);
          }),
      remote_connections_modal_(
          &current_theme_,
          &remote_config_store_,
          [this](const std::string& text) {
              return clipboard_controller_.WriteText(text);
          },
          [this](ClipboardController::PasteCallback deliver) {
              clipboard_controller_.ReadText(std::move(deliver));
          },
          [this](
              const std::string& project_id,
              const std::string& connection_id,
              std::string& error) {
              return SetProjectConnection(project_id, connection_id, error);
          },
          [this](const std::string& connection_id, std::string& error) {
              return CanDeleteRemoteConnection(connection_id, error);
          }),
      remote_files_modal_(
          &current_theme_,
          &remote_config_store_,
          &file_manager_,
          [this] { return CurrentSidebarDirectory(); },
          [this](const std::filesystem::path& path, std::string& error) {
              const bool opened = OpenFile(Utf8FromPath(path), error);
              if (opened) {
                  FocusEditor();
                  screen_.PostEvent(ftxui::Event::Custom);
              }
              return opened;
          },
          [this](const std::string& text) {
              return clipboard_controller_.WriteText(text);
          }),
      git_modal_(
          &current_theme_,
          &git_manager_,
          [this](const std::filesystem::path& path, std::string& error) {
              const bool opened = OpenFile(Utf8FromPath(path), error);
              if (opened) {
                  FocusEditor();
                  screen_.PostEvent(ftxui::Event::Custom);
              }
              return opened;
          },
          [this](
              const std::string& left_title,
              const std::string& left_content,
              const std::string& right_title,
              const std::string& right_content,
              std::string& error) {
              return OpenGitCompareDocuments(
                  left_title,
                  left_content,
                  right_title,
                  right_content,
                  error);
          },
          [this](const std::string& text) {
              return clipboard_controller_.WriteText(text);
          },
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      tts_modal_(
          &current_theme_,
          &cloud_tts_pipeline_,
          &editor_config_,
          [this](bool force_rebuild) {
              QueueTtsBookPreparationFromCursor(force_rebuild);
          },
          [this] { screen_.PostEvent(ftxui::Event::Custom); },
          [this](TtsHeaderButton button) { SetTtsHeaderActiveButton(button); }),
      view_layout_modal_(
          &current_theme_,
          [this] { return CurrentViewLayoutSnapshot(); },
          [this](int layout_index) {
              SetEditorLayoutMode(layout_index == 1 ? EditorLayoutMode::TwoColumns :
                  layout_index == 2 ? EditorLayoutMode::ThreeColumns : EditorLayoutMode::Single);
          },
          [this](size_t pane_index, size_t session_index) {
              AssignSessionToEditorPane(pane_index, session_index);
          },
          [this] { EqualizeEditorPaneWidths(); },
          [this] { CloseViewLayoutModal(); }),
      distraction_options_modal_(
          &current_theme_,
          [this] { return distraction_controller_.Settings(); },
          [this](DistractionModeSettings settings) { ApplyDistractionSettings(settings); },
          [this](const std::string& command_id) { RunCommand(command_id); },
          [this] { CloseDistractionOptionsModal(); }),
      quick_ai_modal_(
          &current_theme_,
          [this] { screen_.PostEvent(ftxui::Event::Custom); },
          [this] { CloseQuickAiModal(); },
          [this](const std::string& text) { return clipboard_controller_.WriteText(text); }),
      ai_settings_modal_(
          &current_theme_,
          &editor_config_,
          [this] { screen_.PostEvent(ftxui::Event::Custom); },
          [this](const std::string& text) { return clipboard_controller_.WriteText(text); }),
      assistant_settings_modal_(
          &current_theme_,
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      theme_colors_modal_(
          &current_theme_,
          [this](const std::map<std::string, std::string>& overrides, std::string& error) {
              Theme updated = FindThemeByName(themes_, current_theme_.name);
              if (!ApplyThemeColorOverrides(updated, overrides, &error)) {
                  return false;
              }
              const auto previous_overrides = editor_config_.theme_color_overrides;
              editor_config_.theme_color_overrides[current_theme_.name] = overrides;
              if (!editor_config_.Persist()) {
                  editor_config_.theme_color_overrides = previous_overrides;
                  error = "Could not save theme color settings.";
                  return false;
              }
              current_theme_ = std::move(updated);
              connect_workspace_->UpdateBrowserTheme();
              theme_dialog_.SetTheme(&current_theme_);
              active_action_ = "Updated colors for " + current_theme_.name;
              screen_.PostEvent(ftxui::Event::Custom);
              return true;
          },
          [this](std::string& error) {
              const auto previous_overrides = editor_config_.theme_color_overrides;
              editor_config_.theme_color_overrides.erase(current_theme_.name);
              if (!editor_config_.Persist()) {
                  editor_config_.theme_color_overrides = previous_overrides;
                  error = "Could not save the reset theme colors.";
                  return false;
              }
              current_theme_ = FindThemeByName(themes_, current_theme_.name);
              connect_workspace_->UpdateBrowserTheme();
              theme_dialog_.SetTheme(&current_theme_);
              active_action_ = "Reset colors for " + current_theme_.name;
              screen_.PostEvent(ftxui::Event::Custom);
              return true;
          },
          [this] { CloseThemeColorsModal(); }),
      theme_dialog_(
          &current_theme_,
          [this](const std::string& theme_name) { PreviewTheme(theme_name); },
          [this](const std::string& theme_name) { SelectTheme(theme_name); },
          [this] { CloseThemeDialog(); }),
      unsaved_changes_dialog_(
          &current_theme_,
          [this] { SavePendingChanges(); },
          [this] { FinishPendingClose(); },
          [this] { CancelUnsavedChanges(); }) {
    menu_bar_ = ftxui::Make<MenuBarComponent>(
        [this](int menu_index, int item_index) {
            this->RunDropdownAction(menu_index, item_index);
        },
        WorkspaceTabs(workspace_mode_, {
            [this] { ShowDocumentsWorkspace(); },
            [this] { ShowNotesWorkspace(); },
            [this] { ShowActionsWorkspace(); },
            [this] { ShowConnectWorkspace(); },
        }),
        &current_theme_);
    InitializeCommands();
    InitializeMenuShortcuts();
    InitializeTextShortcuts();
    shortcut_registry_.SetOverrides(shortcut_store_.Load());
    menu_bar_->SetShortcutProvider([this](const std::string& command_id) {
        return shortcut_registry_.EffectiveShortcut(ShortcutContext::Menu, command_id);
    });
    menu_bar_->RefreshShortcutLabels();
    InitializeProjects();

    notes_workspace_component_ = ftxui::Make<notes::NotesWorkspaceComponent>(
        &current_theme_,
        [this](const std::string& message) {
            active_action_ = message;
            screen_.PostEvent(ftxui::Event::Custom);
        },
        [this](const std::string& text) { return clipboard_controller_.WriteText(text); });
    auto notes_component = std::static_pointer_cast<notes::NotesWorkspaceComponent>(
        notes_workspace_component_);
    notes_component->SetClipboardReader([this](ClipboardController::PasteCallback deliver) {
        clipboard_controller_.ReadText(std::move(deliver));
    });
    notes_component->SetColumnsChangedCallback([this](int columns) {
        editor_config_.notes_editor_columns = columns;
        SaveConfig();
    });
    SyncNotesProjectContext();

    actions_workspace_component_ = ftxui::Make<actions::ActionsWorkspaceComponent>(
        &current_theme_,
        [this](const std::string& message) { active_action_ = message; screen_.PostEvent(ftxui::Event::Custom); },
        [this](const std::string& text) { return clipboard_controller_.WriteText(text); },
        [this] { RefreshActionsSidebar(); });
    auto actions_component = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    actions_component->SetClipboardReader([this](ClipboardController::PasteCallback deliver) {
        clipboard_controller_.ReadText(std::move(deliver));
    });
    actions_component->SetMissingProjectCheck([this](const std::string& id) {
        return project_store_.IsUsable() && !project_store_.FindById(id);
    });
    SyncActionsProjectContext();
    {
        std::string error;
        if (!FinishProjectGroupDetachments(error)) active_action_ = error;
    }
    // Reconcile group names if the registry was saved before a group write failed.
    for (const ProjectRecord& project : project_store_.Projects()) {
        std::string error;
        if (!UpdateProjectGroupNames(project, error)) active_action_ = error;
    }
    RefreshActionsSidebar();

    editor_find_target_ = std::make_unique<EditorFindTarget>([this]() {
        return std::static_pointer_cast<EditorComponent>(text_editor_).get();
    });
    notes_find_target_ = std::make_unique<NotesFindTarget>([this]() {
        return std::static_pointer_cast<notes::NotesWorkspaceComponent>(
            notes_workspace_component_).get();
    });
    actions_find_target_ = std::make_unique<ActionsFindTarget>([this]() {
        return std::static_pointer_cast<actions::ActionsWorkspaceComponent>(
            actions_workspace_component_).get();
    });
    find_replace_panel_ = std::make_unique<FindReplacePanel>(
        &current_theme_,
        [this](const std::string& message) {
            active_action_ = message;
            screen_.PostEvent(ftxui::Event::Custom);
        },
        [this] { screen_.PostEvent(ftxui::Event::Custom); },
        [this] { CloseFindPanel(); });

    top_bar_row_ = ftxui::Make<TopBarRowComponent>(
        &current_theme_,
        TopBarRowComponent::Callbacks{
            [this](const std::string& command_id) { RunCommand(command_id); },
            [this] { return tts_modal_.ShouldShowHeaderControls(); },
            [this] { return tts_modal_.HeaderStatus(); },
            [this] { return tts_header_active_button_; },
            [this] { return distraction_controller_.Enabled(); },
            [this] { return CurrentDistractionTopBarState(); },
            [this] { OpenDistractionPagePanel(); },
            [this] { return workspace_mode_ == WorkspaceMode::Actions; },
        });

    bottom_bar_row_ = ftxui::Make<BottomBarRowComponent>(
        &current_theme_,
        BottomBarRowComponent::Callbacks{
            [this] { return CurrentBottomBarRowState(); },
            [this](const std::string& command_id) { RunCommand(command_id); },
        });

    document_workspace_.SetEditorPaneCount(3);
    editor_pane_components_.clear();
    editor_pane_components_.push_back(text_editor_);
    editor_pane_components_.push_back(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_));
    editor_pane_components_.push_back(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_));

    for (size_t pane_index = 0; pane_index < editor_pane_components_.size(); ++pane_index) {
        auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
        if (!editor) continue;
        editor->SetExternalMouseHandler([this](ftxui::Event event) {
            if (event.mouse().button != ftxui::Mouse::Left ||
                event.mouse().motion != ftxui::Mouse::Pressed) {
                return false;
            }
            const int x = event.mouse().x;
            const int y = event.mouse().y;
            for (size_t target_pane = 0; target_pane < VisibleEditorPaneCount() &&
                 target_pane < editor_pane_components_.size(); ++target_pane) {
                auto target_editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[target_pane]);
                const auto doc = target_editor ? target_editor->GetSession() : nullptr;
                if (!doc || !doc->git_compare) continue;
                const auto& previous = git_compare_prev_boxes_[target_pane];
                const auto& next = git_compare_next_boxes_[target_pane];
                if (previous.x_max > previous.x_min && previous.y_max >= previous.y_min &&
                    previous.Contain(x, y)) {
                    NavigateGitCompare(target_pane, false);
                    return true;
                }
                if (next.x_max > next.x_min && next.y_max >= next.y_min && next.Contain(x, y)) {
                    NavigateGitCompare(target_pane, true);
                    return true;
                }
            }
            return false;
        });
    }

    editor_pane_renderers_.clear();
    for (size_t pane_index = 0; pane_index < editor_pane_components_.size(); ++pane_index) {
        auto pane_renderer = ftxui::Renderer(
            editor_pane_components_[pane_index],
            [this, pane_index] { return RenderEditorPane(pane_index); });
        editor_pane_renderers_.push_back(ftxui::CatchEvent(
            pane_renderer,
            [this, pane_index](ftxui::Event event) {
                return app_event_dispatcher_.HandleEditorPaneEvent(pane_index, event);
            }));
    }

    // Keep one stable component tree. A component cannot safely belong to the
    // single-, two-, and three-pane containers at the same time: TakeFocus()
    // would follow an ambiguous parent chain and reset the workspace to the
    // single-pane tab when a modal closes. RenderEditorPane() hides panes that
    // are not part of the selected layout.
    editor_workspace_container_ = ftxui::Container::Horizontal(editor_pane_renderers_);

    RestoreOpenedSessions();
    EnsureOneOpenSession();
    UpdateFileMenuLabels();
    UpdateOptionsMenuLabels();

    auto workspace_content = ftxui::Container::Tab({
        editor_workspace_container_,
        notes_workspace_component_,
        actions_workspace_component_,
    }, &workspace_mode_index_);
    connect_workspace_ = ftxui::Make<connect_ui::ConnectWorkspace>(
        current_theme_, [this] { screen_.PostEvent(ftxui::Event::Custom); },
        [this] { OpenFilesModal(FilesModalMode::SendConnectFile); },
        [] { return FileManager::UserDownloadsDirectory(false); },
        EditorConfig::DefaultConfigPath().parent_path() / "localsend.pem",
         [this] { OpenTextLTConfigModal(); },
         [this] { OpenTelegramSettingsModal(); },
         [this] { OpenIrcSettingsModal(); },
         [this](const std::string& text) { return clipboard_controller_.WriteText(text); });
    connect_workspace_->RegisterContentHandler(MakeNoteConnectHandler(*notes_component));
    connect_workspace_->RegisterContentHandler(MakeActionConnectHandler(
        *std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)));
    auto body_content = ftxui::Container::Tab({
        ftxui::Container::Horizontal({sidebar_panel_, workspace_content}),
        connect_workspace_,
    }, &connect_body_index_);
    body_container_ = ftxui::CatchEvent(body_content, [this](ftxui::Event event) {
        return app_event_dispatcher_.HandleBodyEvent(event);
    });

    // FIXED: Added missing underscore to match class declaration
    main_container_ = ftxui::Container::Vertical({
        top_bar_row_,
        menu_bar_,
        body_container_,
        bottom_bar_row_,
    });

    ftxui::InputOption goto_line_input_option;
    goto_line_input_option.multiline = false;
    goto_line_input_option.on_enter = [this] { SubmitGoToLine(); };
    goto_line_input_option.cursor_position = &goto_line_input_cursor_position_;
    goto_line_input_option.transform = [this](ftxui::InputState state) {
        return current_theme_.InputTransform(std::move(state));
    };
    goto_line_input_component_ = ClipboardController::MakeInput(
        &goto_line_input_, "line number", goto_line_input_option);
    goto_line_go_button_ = ftxui::Button(MakePopupFlatButtonOption(
        "Go", [this] { SubmitGoToLine(); }, &current_theme_, ButtonRole::Primary));
    goto_line_cancel_button_ = ftxui::Button(MakePopupFlatButtonOption(
        "Cancel", [this] { CloseGoToLinePanel(); }, &current_theme_, ButtonRole::Cancel));
    goto_line_popup_container_ = ftxui::Container::Horizontal({
        goto_line_input_component_,
        goto_line_go_button_,
        goto_line_cancel_button_,
    });

    ftxui::InputOption project_name_option;
    project_name_option.multiline = false;
    project_name_option.on_enter = [this] { SubmitRenameProject(); };
    project_name_option.transform = [this](ftxui::InputState state) {
        return current_theme_.InputTransform(std::move(state));
    };
    project_name_selection_ = std::make_shared<InputSelection>();
    project_name_input_ = MakeSelectableInput(project_name_, project_name_selection_->cursor,
        "Project name", std::move(project_name_option), project_name_selection_);
    rename_project_button_ = ftxui::Button(MakePopupFlatButtonOption(
        "Rename", [this] { SubmitRenameProject(); }, &current_theme_, ButtonRole::Primary));
    cancel_rename_project_button_ = ftxui::Button(MakePopupFlatButtonOption(
        "Cancel", [this] { CloseRenameProject(); }, &current_theme_, ButtonRole::Cancel));
    rename_project_popup_ = ftxui::CatchEvent(
        ftxui::Container::Vertical({
            project_name_input_,
            ftxui::Container::Horizontal({rename_project_button_, cancel_rename_project_button_}),
        }),
        [this](ftxui::Event event) {
            if (event != ftxui::Event::Escape) return false;
            CloseRenameProject();
            return true;
        });

    remove_project_popup_ = ftxui::CatchEvent(
        ftxui::Renderer([this](bool) { return RenderRemoveProjectPopup(); }),
        [this](ftxui::Event event) { return HandleRemoveProjectEvent(std::move(event)); });

    // GitSettingsModal used to create its component lazily in Open(). Every
    // entry passed to Container::Tab must already be a valid, stable component.
    git_settings_modal_.Configure(
        &current_theme_,
        &git_manager_,
        [this](const std::string& text) { return clipboard_controller_.WriteText(text); },
        [this] { CloseGitSettingsModal(); });

    connect_settings_modal_.Configure(current_theme_, *connect_workspace_,
        [this](const std::string& text) { return clipboard_controller_.WriteText(text); },
        [this] { CloseConnectSettingsModal(); });
    telegram_settings_modal_.Configure(
        current_theme_, connect_workspace_->Telegram(),
        [this] { CloseTelegramSettingsModal(); },
        [this] { screen_.PostEvent(ftxui::Event::Custom); },
        [this](TelegramSettingsModal::ClipboardPaste deliver) {
            clipboard_controller_.ReadText(std::move(deliver));
        });
    irc_settings_modal_.Configure(
        current_theme_, connect_workspace_->Irc(),
        EditorConfig::DefaultConfigPath().parent_path() / "irc-settings.json",
        [this] { CloseIrcSettingsModal(); },
        [this] { screen_.PostEvent(ftxui::Event::Custom); },
        [this](const std::string& text) { return clipboard_controller_.WriteText(text); });
    textlt_config_modal_.Configure(current_theme_, *connect_workspace_,
        [this](const std::string& text) { return clipboard_controller_.WriteText(text); },
        [this] { CloseTextLTConfigModal(); });

    root_container_ = ftxui::Container::Tab({
        main_container_,
        help_dialog_.View(),
        keyboard_shortcuts_modal_.View(),
        sidebar_shortcuts_modal_.View(),
        custom_processor_builder_modal_.View(),
        theme_dialog_.View(),
        theme_colors_modal_.View(),
        goto_line_popup_container_,
        unsaved_changes_dialog_.View(),
        recent_files_modal_.View(),
        search_files_modal_.View(),
        files_modal_.View(),
        text_processors_modal_.View(),
        remote_connections_modal_.View(),
        remote_files_modal_.View(),
        git_modal_.View(),
        git_settings_modal_.View(),
        tts_modal_.View(),
        view_layout_modal_.View(),
        distraction_options_modal_.View(),
        quick_ai_modal_.View(),
        ai_settings_modal_.View(),
        assistant_settings_modal_.View(),
        rename_project_popup_,
        remove_project_popup_,
        connect_settings_modal_.View(),
        textlt_config_modal_.View(),
        telegram_settings_modal_.View(),
        irc_settings_modal_.View(),
    }, &active_layer_index_);

    renderer_ = ftxui::Renderer(root_container_, [this] { return Render(); });
    global_shortcuts_ = ftxui::CatchEvent(
        renderer_, [this](ftxui::Event event) { return HandleGlobalEvent(event); });

    FocusEditor();
}

TextltApp::TextltApp(const std::vector<std::string>& files_to_open)
    : TextltApp() {
    if (!files_to_open.empty()) {
        document_workspace_.ClearSessions();
        std::static_pointer_cast<EditorComponent>(text_editor_)
            ->SetSession(std::make_shared<DocumentSession>());
    }
    InitializeWithFiles(files_to_open);
    EnsureOneOpenSession();
    PersistOpenedSessions();
}

void TextltApp::Run() {
    screen_.ForceHandleCtrlC(false);
    BracketedPasteModeGuard bracketed_paste_mode_guard;
    screen_.Loop(global_shortcuts_);
    connect_workspace_->Stop();
    if (notes_workspace_component_) {
        std::string error;
        std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->Save(error);
    }
    if (actions_workspace_component_) {
        std::string error;
        std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->Save(error);
    }
    PersistOpenedSessions();
}
} // namespace textlt
