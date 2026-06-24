#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
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
      text_editor_(ftxui::Make<EditorComponent>(&editor_config_, &current_theme_)),
      sidebar_panel_(ftxui::Make<SidebarPanel>(
          [this](const std::filesystem::path& path) { OpenSidebarFile(path); },
          [this](size_t index) { ActivateOpenDocument(index); },
          &current_theme_,
          &git_manager_,
          &editor_config_)),
      file_dialog_(&current_theme_, [this](
          FilePromptMode mode,
          const std::string& path,
          std::string& error) {
          return ConfirmFileDialog(mode, path, error);
      }),
      path_operation_dialog_(&current_theme_, [this](
          PathOperationMode mode,
          const std::string& from,
          const std::string& to,
          std::string& error) {
          return ConfirmPathOperation(mode, from, to, error);
      }),
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
              }),
              tts_modal_(
          &current_theme_,
          &cloud_tts_pipeline_,
          [this] { QueueTtsBookPreparationFromCursor(); }),
      ai_actions_modal_(&current_theme_),
      assistant_settings_modal_(
          &current_theme_,
          [this] { screen_.PostEvent(ftxui::Event::Custom); }),
      theme_dialog_(
          &current_theme_,
          [this](const std::string& theme_name) { PreviewTheme(theme_name); },
          [this](const std::string& theme_name) { SelectTheme(theme_name); }),
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
    RestoreOpenedDocuments();
    EnsureOneOpenDocument();
    UpdateFileMenuLabels();
    UpdateOptionsMenuLabels();

    auto body_content = ftxui::Container::Horizontal({
        sidebar_panel_,
        text_editor_,
    });
    body_container_ = ftxui::CatchEvent(body_content, [this](ftxui::Event event) {
        if (event == ftxui::Event::Tab &&
            focused_layer_ == 0 &&
            (!menu_bar_ || !menu_bar_->IsDropdownOpen()) &&
            !file_dialog_.IsOpen() &&
            !help_dialog_.IsOpen() &&
            !recent_files_modal_.IsOpen() &&
            !search_files_modal_.IsOpen() &&
            !tts_modal_.IsOpen() &&
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
        file_dialog_.View(),
        path_operation_dialog_.View(),
        help_dialog_.View(),
        theme_dialog_.View(),
        find_panel_container_,
        goto_line_input_component_,
        unsaved_changes_dialog_.View(),
        tts_modal_.View(),
        ai_actions_modal_.View(),
        assistant_settings_modal_.View(),
    }, &focused_layer_);

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
    screen_.Loop(global_shortcuts_);
    PersistOpenedDocuments();
}
} // namespace textlt
