#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "app_resources.hpp"
#include "ftxui/component/component_options.hpp"
#include "theme.hpp"
#include "ui_button.hpp"
#include "file_manager.hpp"
#include "editor/document_session.hpp"

namespace textlt {

namespace {

class BracketedPasteModeGuard {
public:
    BracketedPasteModeGuard() {
        std::cout << "\x1B[?2004h" << std::flush;
    }

    ~BracketedPasteModeGuard() {
        std::cout << "\x1B[?2004l" << std::flush;
    }
};

ftxui::ButtonOption MakeFindPanelTextButtonOption(
    std::string label,
    std::function<void()> on_click,
    const Theme* theme,
    std::function<bool()> is_active = {},
    ButtonRole role = ButtonRole::Default,
    std::string icon = {}) {
    ButtonSpec base_spec = ButtonSpecFromLabel(
        std::move(label),
        role,
        ButtonVariant::AccentEdges,
        ButtonSize::Compact,
        std::move(icon));

    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = ButtonCaptionText(base_spec);
    option.on_click = std::move(on_click);
    option.transform = [theme,
                        is_active = std::move(is_active),
                        base_spec = std::move(base_spec)](const ftxui::EntryState& state) {
        const Theme fallback_theme;
        const Theme& resolved_theme = theme ? *theme : fallback_theme;
        ButtonSpec spec = base_spec;
        spec.selected = is_active && is_active();
        return RenderButton(resolved_theme, spec, state.focused || state.active);
    };
    return option;
}

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
      current_theme_(FindThemeByName(themes_, editor_config_.active_theme_name)),
      screen_(ftxui::App::Fullscreen()),
      app_event_dispatcher_(*this),
      file_manager_(),
      document_workspace_(),
      layout_controller_(document_workspace_, &distraction_controller_),
      document_file_controller_(file_manager_, document_workspace_),
      remote_config_store_(),
      text_editor_(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_)),
      sidebar_panel_(ftxui::Make<SidebarPanel>(
          [this](const std::filesystem::path& path) { OpenSidebarFile(path); },
          [this](size_t index) { ActivateOpenSession(index); },
          &current_theme_,
          &git_manager_,
          [this] { return document_file_controller_.FavoriteFilePaths(); },
          [this](const std::filesystem::path& path) {
              document_file_controller_.RemoveFavorite(path);
              UpdateFileMenuLabels();
              active_action_ = "Removed favorite " + path.string();
              screen_.PostEvent(ftxui::Event::Custom);
          },
          [this] { AddActiveFavoriteFromSidebar(); },
          [this](size_t index) { CloseSidebarOpenedFile(index); },
          [this] { CloseAllOpenedFiles(); },
          [this] { OpenFilesModal(FilesModalMode::Manage); },
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
          [this] { ShowNotesWorkspace(); })),
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
          [this] { return clipboard_controller_.ReadText(); },
          [this](const std::string& text) { clipboard_controller_.WriteText(text); }),
      recent_files_modal_(
          &current_theme_,
          &document_file_controller_,
          [this](const std::filesystem::path& path, std::string& error) {
              const bool opened = OpenFile(path.string(), error);
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
              [this] {
                  return clipboard_controller_.ReadText();
              },
              [this](const std::string& text) {
                  clipboard_controller_.WriteText(text);
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
          [this](const std::string& text) {
              CopyFileModalPathText(text);
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
              bool whole_document,
              std::string& text,
              std::string& error) {
              return GetTextProcessorTargetText(whole_document, text, error);
          },
          [this](
              bool whole_document,
              const std::string& text,
              std::string& error) {
              return ReplaceTextProcessorTargetText(whole_document, text, error);
          }),
      remote_connections_modal_(
          &current_theme_,
          &remote_config_store_,
          [this](const std::string& text) {
              clipboard_controller_.WriteText(text);
          }),
      remote_files_modal_(
          &current_theme_,
          &remote_config_store_,
          &file_manager_,
          [this] { return CurrentSidebarDirectory(); },
          [this](const std::filesystem::path& path, std::string& error) {
              const bool opened = OpenFile(path.string(), error);
              if (opened) {
                  FocusEditor();
                  screen_.PostEvent(ftxui::Event::Custom);
              }
              return opened;
          },
          [this](const std::string& text) {
              clipboard_controller_.WriteText(text);
          }),
      git_modal_(
          &current_theme_,
          &git_manager_,
          [this](const std::filesystem::path& path, std::string& error) {
              const bool opened = OpenFile(path.string(), error);
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
              clipboard_controller_.WriteText(text);
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
              layout_controller_.SetModeByIndex(layout_index);
              BindEditorComponentsToWorkspace();
              SetActiveEditorPane(document_workspace_.ActiveEditorPaneIndex());
              active_action_ = "View layout: " + layout_controller_.ModeLabel();
              screen_.PostEvent(ftxui::Event::Custom);
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
      ai_actions_modal_(
          &current_theme_,
          &editor_config_,
          [this](bool whole_document, AiDocumentTarget& target, std::string& error) {
              const auto editor = ActiveEditor();
              target.session = editor ? editor->GetSession() : nullptr;
              if (!target.session || !editor) {
                  error = "No active document.";
                  return false;
              }
              return target.session->CaptureAiTransformTargetAt(
                  editor->GetCursorRow(),
                  editor->GetCursorCol(),
                  whole_document,
                  target.range,
                  error);
          },
          [this](const AiDocumentTarget& target,
                 const std::string& text,
                 std::string& error) {
              const auto active_editor = ActiveEditor();
              const std::shared_ptr<DocumentSession> active_session =
                  active_editor ? active_editor->GetSession() : nullptr;
              if (!target.session || active_session != target.session) {
                  error = "The active document changed while the AI request was running.";
                  return false;
              }
              if (!target.session->ReplaceAiTransformTarget(target.range, text, error)) {
                  return false;
              }
              const auto editor = ActiveEditor();
              if (editor) {
                  editor->SetSession(target.session);
              }
              active_action_ = target.range.whole_document
                  ? "AI action applied to whole document"
                  : "AI action applied to current paragraph";
              screen_.PostEvent(ftxui::Event::Custom);
              return true;
          },
          [this] { screen_.PostEvent(ftxui::Event::Custom); },
          [this](const std::string& message) {
              active_action_ = message;
              screen_.PostEvent(ftxui::Event::Custom);
          },
          [this](bool success, const std::string& message) {
              active_action_ = message;
              if (success && ai_quick_actions_modal_.IsOpen()) {
                  CloseAiQuickActionsModal();
              } else {
                  screen_.PostEvent(ftxui::Event::Custom);
              }
          }),
      ai_quick_actions_modal_(
          &current_theme_,
          [this](AiActionType action, std::string& error) {
              const bool started = ai_actions_modal_.StartQuickAction(action, error);
              if (started) {
                  active_action_ = action == AiActionType::Translate
                      ? "AI quick translation started for current paragraph"
                      : "AI quick editing started for current paragraph";
                  screen_.PostEvent(ftxui::Event::Custom);
              }
              return started;
          },
          [this] { return ai_actions_modal_.QuickStatus(); },
          [this] { ai_actions_modal_.StopQuickAction(); },
          [this] { CloseAiQuickActionsModal(); }),
      ai_settings_modal_(
          &current_theme_,
          &editor_config_,
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      assistant_settings_modal_(
          &current_theme_,
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      theme_dialog_(
          &current_theme_,
          [this](const std::string& theme_name) { PreviewTheme(theme_name); },
          [this](const std::string& theme_name) { SelectTheme(theme_name); },
          [this] { CloseThemeDialog(); }),
      unsaved_changes_dialog_(
          &current_theme_,
          [this] { SaveAndExit(); },
          [this] { DiscardAndExit(); },
          [this] { CloseExitConfirmationDialog(); }) {
    menu_bar_ = ftxui::Make<MenuBarComponent>(
        [this](int menu_index, int item_index) {
            this->RunDropdownAction(menu_index, item_index);
        },
        &current_theme_);
    InitializeCommands();
    InitializeMenuShortcuts();
    InitializeTextShortcuts();
    shortcut_registry_.SetOverrides(shortcut_store_.Load());

    notes_workspace_component_ = ftxui::Make<notes::NotesWorkspaceComponent>(
        &current_theme_,
        [this](const std::string& message) {
            active_action_ = message;
            screen_.PostEvent(ftxui::Event::Custom);
        },
        [this] { ShowDocumentsWorkspace(); },
        [this] { return clipboard_controller_.ReadText(); },
        [this](const std::string& text) { clipboard_controller_.WriteText(text); });

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

    auto documents_body = ftxui::Container::Horizontal({
        sidebar_panel_,
        editor_workspace_container_,
    });
    auto body_content = ftxui::Container::Tab({
        documents_body,
        notes_workspace_component_,
    }, &workspace_mode_index_);
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

    ftxui::InputOption find_input_option;
    find_input_option.multiline = false;
    find_input_option.on_change = [this] { RefreshFindMatches(); };
    find_input_option.on_enter = [this] { FindNext(); };
    find_input_option.cursor_position = &find_input_cursor_position_;
    find_input_ = ftxui::Input(&find_query_, "find text", find_input_option);

    ftxui::InputOption replace_find_input_option = find_input_option;
    replace_find_input_option.cursor_position = &replace_find_input_cursor_position_;
    replace_find_input_ = ftxui::Input(&find_query_, "find text", replace_find_input_option);

    ftxui::InputOption replace_input_option;
    replace_input_option.multiline = false;
    replace_input_option.on_enter = [this] { FindNext(); };
    replace_input_option.cursor_position = &replace_input_cursor_position_;
    replace_input_ = ftxui::Input(&replace_text_, "replacement", replace_input_option);

    ftxui::InputOption goto_line_input_option;
    goto_line_input_option.multiline = false;
    goto_line_input_option.on_enter = [this] { SubmitGoToLine(); };
    goto_line_input_option.cursor_position = &goto_line_input_cursor_position_;
    goto_line_input_option.transform = [this](ftxui::InputState state) {
        return current_theme_.InputTransform(std::move(state));
    };
    goto_line_input_component_ = ftxui::Input(
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

    find_paste_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Paste", [this] { PasteIntoFindPanelInput(); }, &current_theme_, {}, ButtonRole::Navigation));
    find_clear_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Clear", [this] { ClearFindPanelFields(); }, &current_theme_));
    replace_paste_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Paste", [this] { PasteIntoFindPanelInput(); }, &current_theme_, {}, ButtonRole::Navigation));
    replace_clear_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Clear", [this] { ClearFindPanelFields(); }, &current_theme_));
    find_next_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Find Next", [this] { FindNext(); }, &current_theme_, {}, ButtonRole::Navigation));
    find_previous_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Find Prev", [this] { FindPrevious(); }, &current_theme_));
    replace_next_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Replace Next", [this] { ReplaceNext(); }, &current_theme_));
    replace_all_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Replace All", [this] { ReplaceAll(); }, &current_theme_));

    auto search_filter_transform = [this](const ftxui::EntryState& state) {
        ftxui::Element item = ftxui::text(
            std::string(state.state ? "[X] " : "[ ] ") + state.label);
        if (state.active) {
            item = item | ftxui::bold;
        }
        if (state.focused) {
            item = item |
                ftxui::bgcolor(current_theme_.menu_foreground) |
                ftxui::color(current_theme_.menu_background);
        } else {
            item = item | ftxui::color(current_theme_.menu_foreground);
        }
        return item;
    };

    ftxui::CheckboxOption match_case_option = ftxui::CheckboxOption::Simple();
    match_case_option.transform = search_filter_transform;
    match_case_option.on_change = [this] {
        std::static_pointer_cast<EditorComponent>(text_editor_)->ToggleSearchMatchCase();
        SaveConfig();
        RefreshFindMatches();
        screen_.PostEvent(ftxui::Event::Custom);
    };
    search_match_case_checkbox_ = ftxui::Checkbox(
        "Match Case", &editor_config_.search_match_case, match_case_option);

    ftxui::CheckboxOption whole_word_option = ftxui::CheckboxOption::Simple();
    whole_word_option.transform = search_filter_transform;
    whole_word_option.on_change = [this] {
        std::static_pointer_cast<EditorComponent>(text_editor_)->ToggleSearchWholeWord();
        SaveConfig();
        RefreshFindMatches();
        screen_.PostEvent(ftxui::Event::Custom);
    };
    search_whole_word_checkbox_ = ftxui::Checkbox(
        "Whole Word", &editor_config_.search_whole_word, whole_word_option);

    // GitSettingsModal used to create its component lazily in Open(). Every
    // entry passed to Container::Tab must already be a valid, stable component.
    git_settings_modal_.Configure(
        &current_theme_,
        &git_manager_,
        [this](const std::string& text) { clipboard_controller_.WriteText(text); },
        [this] { CloseGitSettingsModal(); });

    find_panel_find_container_ = ftxui::Container::Horizontal({
        find_input_,
        ftxui::Container::Vertical({
            find_paste_button_,
            find_clear_button_,
        }),
        ftxui::Container::Vertical({
            find_next_button_,
            find_previous_button_,
        }),
    });
    find_panel_replace_container_ = ftxui::Container::Horizontal({
        replace_find_input_,
        replace_input_,
        ftxui::Container::Vertical({
            replace_paste_button_,
            replace_clear_button_,
        }),
        ftxui::Container::Vertical({
            replace_next_button_,
            replace_all_button_,
        }),
    });
    find_panel_fields_container_ = ftxui::Container::Tab({
        find_panel_find_container_,
        find_panel_replace_container_,
    }, &search_panel_tab_index_);
    find_panel_filters_container_ = ftxui::Container::Horizontal({
        search_match_case_checkbox_,
        search_whole_word_checkbox_,
    });
    find_panel_container_ = ftxui::Container::Vertical({
        find_panel_fields_container_,
        find_panel_filters_container_,
    });

    root_container_ = ftxui::Container::Tab({
        main_container_,
        help_dialog_.View(),
        keyboard_shortcuts_modal_.View(),
        sidebar_shortcuts_modal_.View(),
        custom_processor_builder_modal_.View(),
        theme_dialog_.View(),
        find_panel_container_,
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
        ai_actions_modal_.View(),
        ai_quick_actions_modal_.View(),
        ai_settings_modal_.View(),
        assistant_settings_modal_.View(),
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
    if (notes_workspace_component_) {
        std::string error;
        std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->Save(error);
    }
    PersistOpenedSessions();
}
} // namespace textlt
