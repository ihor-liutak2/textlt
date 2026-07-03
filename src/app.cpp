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

#include "app_resources.hpp"
#include "ftxui/component/component_options.hpp"
#include "theme.hpp"
#include "file_manager.hpp"
#include "document.hpp"

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
    const Theme* theme) {
    ftxui::ButtonOption option = ftxui::ButtonOption::Simple();
    option.label = std::move(label);
    option.on_click = std::move(on_click);
    option.transform = [theme](const ftxui::EntryState& state) {
        ftxui::Element button = ftxui::text("[" + state.label + "]");
        if (theme && (state.focused || state.active)) {
            return button |
                ftxui::bgcolor(theme->menu_foreground) |
                ftxui::color(theme->menu_background);
        }
        if (theme) {
            return button | ftxui::color(theme->menu_foreground);
        }
        return button;
    };
    return option;
}

} // namespace

TextltApp::TextltApp()
    : config_manager_("config.json"),
      editor_config_(config_manager_.Load()),
      themes_(LoadThemesFromConfiguredLocations()),
      current_theme_(FindThemeByName(themes_, editor_config_.active_theme_name)),
      screen_(ftxui::ScreenInteractive::Fullscreen()),
      file_manager_(),
      remote_config_store_(),
      text_editor_(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_)),
      sidebar_panel_(ftxui::Make<SidebarPanel>(
          [this](const std::filesystem::path& path) { OpenSidebarFile(path); },
          [this](size_t index) { ActivateOpenDocument(index); },
          &current_theme_,
          &git_manager_,
          &editor_config_)),
      help_dialog_(&current_theme_),
      recent_files_modal_(
          &current_theme_,
          &recent_files_history_,
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
                  return ReadSystemClipboard();
              },
              [this](const std::string& text) {
                  WriteSystemClipboard(text);
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
          &remote_config_store_),
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
              WriteSystemClipboard(text);
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
              WriteSystemClipboard(text);
          },
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      tts_modal_(
          &current_theme_,
          &cloud_tts_pipeline_,
          [this](bool force_rebuild) {
              QueueTtsBookPreparationFromCursor(force_rebuild);
          },
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      view_layout_modal_(
          &current_theme_,
          [this] { return CurrentViewLayoutSnapshot(); },
          [this](int layout_index) {
              if (layout_index == 1) {
                  SetEditorLayoutMode(EditorLayoutMode::TwoColumns);
              } else if (layout_index == 2) {
                  SetEditorLayoutMode(EditorLayoutMode::ThreeColumns);
              } else {
                  SetEditorLayoutMode(EditorLayoutMode::Single);
              }
          },
          [this](size_t pane_index) {
              SetActiveEditorPane(pane_index);
              screen_.PostEvent(ftxui::Event::Custom);
          },
          [this](size_t pane_index, size_t document_index) {
              AssignDocumentToEditorPane(pane_index, document_index);
          },
          [this](size_t pane_index, const std::string& role) {
              SetEditorPaneRole(pane_index, role);
          },
          [this] { SplitActiveDocumentToNextPane(); },
          [this] { EqualizeEditorPaneWidths(); },
          [this] { CloseViewLayoutModal(); }),
      ai_actions_modal_(&current_theme_),
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
    recent_files_history_.Load();
    menu_bar_ = ftxui::Make<MenuBarComponent>(
        [this](int menu_index, int item_index) {
            this->RunDropdownAction(menu_index, item_index);
        },
        &current_theme_);
    InitializeCommands();

    title_bar_open_tts_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "TTS",
        [this] { RunCommand("tts.open_modal"); },
        &current_theme_));
    title_bar_tts_play_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Play",
        [this] { RunCommand("tts.play"); },
        &current_theme_));
    title_bar_tts_pause_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Pause",
        [this] { RunCommand("tts.pause"); },
        &current_theme_));
    title_bar_tts_stop_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Stop",
        [this] { RunCommand("tts.stop"); },
        &current_theme_));
    title_bar_tts_next_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Next",
        [this] { RunCommand("tts.next"); },
        &current_theme_));
    title_bar_component_ = ftxui::Renderer(
        ftxui::Container::Horizontal({
            title_bar_open_tts_button_,
            title_bar_tts_play_button_,
            title_bar_tts_pause_button_,
            title_bar_tts_stop_button_,
            title_bar_tts_next_button_,
        }),
        [this] { return RenderTitleBar(); });

    editor_panes_.assign(3, EditorPaneState{});
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
                if (event.is_mouse() &&
                    pane_index < VisibleEditorPaneCount() &&
                    MainViewCanActivateEditorPane()) {
                    SetActiveEditorPane(pane_index);
                    auto editor = std::static_pointer_cast<EditorComponent>(editor_pane_components_[pane_index]);
                    if (editor) {
                        editor->TakeFocus();
                    }
                }
                return false;
            }));
    }

    // Keep one stable component tree. A component cannot safely belong to the
    // single-, two-, and three-pane containers at the same time: TakeFocus()
    // would follow an ambiguous parent chain and reset the workspace to the
    // single-pane tab when a modal closes. RenderEditorPane() hides panes that
    // are not part of the selected layout.
    editor_workspace_container_ = ftxui::Container::Horizontal(editor_pane_renderers_);

    RestoreOpenedDocuments();
    EnsureOneOpenDocument();
    UpdateFileMenuLabels();
    UpdateOptionsMenuLabels();

    auto body_content = ftxui::Container::Horizontal({
        sidebar_panel_,
        editor_workspace_container_,
    });
    body_container_ = ftxui::CatchEvent(body_content, [this](ftxui::Event event) {
        if (event == ftxui::Event::Tab &&
            ActiveLayer() == UiLayer::Main &&
            (!menu_bar_ || !menu_bar_->IsDropdownOpen()) &&
            !help_dialog_.IsOpen() &&
            !recent_files_modal_.IsOpen() &&
            !search_files_modal_.IsOpen() &&
            !files_modal_.IsOpen() &&
            !text_processors_modal_.IsOpen() &&
            !remote_connections_modal_.IsOpen() &&
            !remote_files_modal_.IsOpen() &&
            !git_modal_.IsOpen() &&
            !git_settings_modal_.IsOpen() &&
            !tts_modal_.IsOpen() &&
            !view_layout_modal_.IsOpen() &&
            !ai_actions_modal_.IsOpen() &&
            !assistant_settings_modal_.IsOpen() &&
            !theme_dialog_.IsOpen() &&
            !sidebar_has_focus_) {
            text_editor_->OnEvent(event);
            return true;
        }
        return false;
    });

    // FIXED: Added missing underscore to match class declaration
    main_container_ = ftxui::Container::Vertical({
        title_bar_component_,
        menu_bar_,
        body_container_,
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
    goto_line_input_component_ = ftxui::Input(
        &goto_line_input_, "line number", goto_line_input_option);

    find_paste_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Paste", [this] { PasteIntoFindPanelInput(); }, &current_theme_));
    find_clear_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Clear", [this] { ClearFindPanelFields(); }, &current_theme_));
    replace_paste_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Paste", [this] { PasteIntoFindPanelInput(); }, &current_theme_));
    replace_clear_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Clear", [this] { ClearFindPanelFields(); }, &current_theme_));
    find_next_button_ = ftxui::Button(MakeFindPanelTextButtonOption(
        "Find Next", [this] { FindNext(); }, &current_theme_));
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
        [this](const std::string& text) { WriteSystemClipboard(text); },
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
        theme_dialog_.View(),
        find_panel_container_,
        goto_line_input_component_,
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
        ai_actions_modal_.View(),
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
        open_documents_.clear();
        active_document_index_ = 0;
        std::static_pointer_cast<EditorComponent>(text_editor_)
            ->SetDocument(std::make_shared<Document>());
    }
    InitializeWithFiles(files_to_open);
    EnsureOneOpenDocument();
    PersistOpenedDocuments();
}

void TextltApp::Run() {
    screen_.ForceHandleCtrlC(false);
    BracketedPasteModeGuard bracketed_paste_mode_guard;
    screen_.Loop(global_shortcuts_);
    PersistOpenedDocuments();
}
} // namespace textlt
