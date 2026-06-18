#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "ftxui/component/component_options.hpp"
#include "theme.hpp"
#include "file_manager.hpp"
#include "document.hpp"

namespace textlt {
namespace {

bool CommandAvailable(const std::string& command) {
    const std::string check_command = "command -v " + command + " >/dev/null 2>&1";
    return std::system(check_command.c_str()) == 0;
}

bool IsWslEnvironment() {
    std::error_code error;
    if (std::filesystem::exists("/proc/sys/fs/binfmt_misc/WSLInterop", error)) {
        return true;
    }
    return CommandAvailable("clip.exe");
}

bool WriteTextToPipe(const std::string& command, const std::string& text) {
    FILE* pipe = popen(command.c_str(), "w");
    if (!pipe) {
        return false;
    }

    const size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
    std::fflush(pipe);
    const int close_status = pclose(pipe);
    return written == text.size() && close_status == 0;
}

bool ExistingFile(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() &&
        std::filesystem::exists(path, error) &&
        std::filesystem::is_regular_file(path, error);
}

std::filesystem::path UserHelpFilePath() {
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home) / ".config" / "textlt" / "help.txt";
}

std::filesystem::path ResolveHelpFilePath() {
    const std::filesystem::path user_help = UserHelpFilePath();
    if (ExistingFile(user_help)) {
        return user_help;
    }

    std::error_code error;
    const std::filesystem::path executable_path =
        std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !executable_path.empty()) {
        const std::filesystem::path executable_directory = executable_path.parent_path();
        const std::filesystem::path executable_help = executable_directory / "help.txt";
        if (ExistingFile(executable_help)) {
            return executable_help;
        }

        const std::filesystem::path shared_help =
            executable_directory.parent_path() / "share" / "textlt" / "help.txt";
        if (ExistingFile(shared_help)) {
            return shared_help;
        }
    }

    return "help.txt";
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
          &current_theme_,
          &git_manager_,
          &editor_config_)),
      file_dialog_(&current_theme_, [this](
          FilePromptMode mode,
          const std::string& path,
          std::string& error) {
          return ConfirmFileDialog(mode, path, error);
      }),
      help_dialog_(&current_theme_),
      theme_dialog_(
          &current_theme_,
          [this](const std::string& theme_name) { PreviewTheme(theme_name); },
          [this](const std::string& theme_name) { SelectTheme(theme_name); }) {
    menu_bar_ = ftxui::Make<MenuBarComponent>(
        [this](int menu_index, int item_index) {
            this->RunDropdownAction(menu_index, item_index);
        },
        &current_theme_);
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
    find_input_ = ftxui::Input(&find_query_, "find text", find_input_option);
    replace_find_input_ = ftxui::Input(&find_query_, "find text", find_input_option);

    ftxui::InputOption replace_input_option;
    replace_input_option.multiline = false;
    replace_input_option.on_enter = [this] { FindNext(); };
    replace_input_ = ftxui::Input(&replace_text_, "replacement", replace_input_option);

    ftxui::InputOption goto_line_input_option;
    goto_line_input_option.multiline = false;
    goto_line_input_option.on_enter = [this] { SubmitGoToLine(); };
    goto_line_input_component_ = ftxui::Input(
        &goto_line_input_, "line number", goto_line_input_option);

    find_next_button_ = ftxui::Button("Find Next", [this] { FindNext(); });
    find_previous_button_ = ftxui::Button("Find Prev", [this] { FindPrevious(); });
    replace_next_button_ = ftxui::Button("Replace Next", [this] { ReplaceNext(); });
    replace_all_button_ = ftxui::Button("Replace All", [this] { ReplaceAll(); });

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
        find_next_button_,
        find_previous_button_,
    });
    find_panel_replace_container_ = ftxui::Container::Horizontal({
        replace_find_input_,
        replace_input_,
        replace_next_button_,
        replace_all_button_,
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

    exit_save_button_ = ftxui::Button("Save", [this] { SaveAndExit(); });
    exit_discard_button_ = ftxui::Button("Don't Save", [this] { DiscardAndExit(); });
    exit_cancel_button_ = ftxui::Button("Cancel", [this] { CloseExitConfirmationDialog(); });
    exit_confirmation_container_ = ftxui::Container::Horizontal({
        exit_save_button_,
        exit_discard_button_,
        exit_cancel_button_,
    });

    root_container_ = ftxui::Container::Tab({
        main_container_,
        file_dialog_.View(),
        help_dialog_.View(),
        theme_dialog_.View(),
        find_panel_container_,
        goto_line_input_component_,
        exit_confirmation_container_,
    }, &focused_layer_);

    renderer_ = ftxui::Renderer(root_container_, [this] { return Render(); });
    global_shortcuts_ = ftxui::CatchEvent(
        renderer_, [this](ftxui::Event event) { return HandleGlobalEvent(event); });

    FocusEditor();
}

TextltApp::TextltApp(const std::vector<std::string>& files_to_open)
    : TextltApp() {
    InitializeWithFiles(files_to_open);
}

void TextltApp::Run() {
    screen_.ForceHandleCtrlC(false);
    screen_.Loop(global_shortcuts_);
}

void TextltApp::CloseDropdown() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenDropdown() {
    UpdateFileMenuLabels();
    if (menu_bar_) {
        menu_bar_->OpenDropdown(0);
    }
    focused_layer_ = 0;
}

void TextltApp::CloseFileDialog() {
    file_dialog_.Close();
    exit_after_save_as_ = false;
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenFileDialog(FilePromptMode mode) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    focused_layer_ = 1;
    
    std::string default_path = std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
    
    // If the active file has no path or is an untitled draft, sync with the sidebar selection.
    if (default_path.empty() || default_path == "Untitled" || default_path == "untitled.txt") {
        auto sidebar_ptr = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
        
        // Dynamic path acquisition directly from the highlighted tree node.
        default_path = sidebar_ptr->GetSelectedDirectoryPath().string();
        
        // Append trailing slash so the path is prepared for filename entry instantly
        if (!default_path.empty() && default_path.back() != '/') {
            default_path += "/";
        }
    }
    
    file_dialog_.Open(mode, default_path);
}

void TextltApp::OpenAboutDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    help_dialog_.OpenContent("About", {
        "textlt — Modern TUI Text Editor",
        "Version: 1.0.0 (Stable Release)",
        "",
        "Author: Ihor Liutak (ihorlt)",
        "Contact: ihorlt@gmail.com",
        "Origin: Ukraine 🇺🇦",
        "",
        "Built with: C++17 & FTXUI Framework",
        "License: MIT License (c) 2026",
    }, true);
    active_action_ = "Opened About";
    focused_layer_ = 2;
}

void TextltApp::OpenHelpDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    help_dialog_.Open(ResolveHelpFilePath().string());
    active_action_ = "Opened Help";
    focused_layer_ = 2;
}

void TextltApp::CloseHelpDialog() {
    help_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::OpenThemeDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    focused_layer_ = 3;
    theme_dialog_.Open(themes_, editor_config_.active_theme_name);
}

void TextltApp::CloseThemeDialog() {
    theme_dialog_.Close();
    focused_layer_ = 0;
    FocusEditor();
}

void TextltApp::ShowExitConfirmationDialog() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    exit_confirmation_open_ = true;
    focused_layer_ = 6;
    exit_save_button_->TakeFocus();
    active_action_ = "Unsaved changes";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseExitConfirmationDialog() {
    exit_confirmation_open_ = false;
    exit_after_save_as_ = false;
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::RequestExit() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    if (editor_ptr->IsDirty()) {
        ShowExitConfirmationDialog();
        return;
    }
    PersistActiveFavoriteCursor();
    screen_.Exit();
}

void TextltApp::SaveAndExit() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const std::string current_path = editor_ptr->CurrentFilePath();
    if (current_path.empty() || current_path == "Untitled" || current_path == "untitled.txt") {
        exit_confirmation_open_ = false;
        exit_after_save_as_ = true;
        OpenFileDialog(FilePromptMode::SaveAs);
        return;
    }

    std::string error;
    if (SaveFile(current_path, error)) {
        PersistActiveFavoriteCursor();
        screen_.Exit();
        return;
    }

    active_action_ = error.empty() ? "Save failed" : error;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::DiscardAndExit() {
    PersistActiveFavoriteCursor();
    screen_.Exit();
}

void TextltApp::ActivateTopMenu() {
    OpenDropdown();
}

void TextltApp::SwitchEditorFocus() {
    if (!editor_config_.show_file_explorer) {
        FocusEditor();
        return;
    }
    if (sidebar_has_focus_) {
        FocusEditor();
    } else {
        FocusSidebar();
    }
}

void TextltApp::FocusEditor() {
    sidebar_has_focus_ = false;
    focused_layer_ = 0;
    text_editor_->TakeFocus();
}

void TextltApp::FocusSidebar() {
    sidebar_has_focus_ = true;
    focused_layer_ = 0;
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->FocusMenu();
}

void TextltApp::OpenFindPanel(bool replace_mode) {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    current_search_mode_ = replace_mode ? SearchMode::Replace : SearchMode::Find;
    search_panel_tab_index_ = replace_mode ? 1 : 0;
    focused_layer_ = 4;
    RefreshFindMatches();
    if (replace_mode) {
        replace_find_input_->TakeFocus();
    } else {
        find_input_->TakeFocus();
    }
}

void TextltApp::CloseFindPanel() {
    current_search_mode_ = SearchMode::None;
    std::static_pointer_cast<EditorComponent>(text_editor_)->ClearSearchHighlights();
    FocusEditor();
}

void TextltApp::OpenGoToLinePanel() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    current_search_mode_ = SearchMode::None;
    std::static_pointer_cast<EditorComponent>(text_editor_)->ClearSearchHighlights();
    show_goto_line_bar_ = true;
    goto_line_input_.clear();
    focused_layer_ = 5;
    goto_line_input_component_->TakeFocus();
    active_action_ = "Go to line";
}

void TextltApp::CloseGoToLinePanel() {
    show_goto_line_bar_ = false;
    FocusEditor();
}

void TextltApp::SubmitGoToLine() {
    try {
        size_t parsed_chars = 0;
        const int line_number = std::stoi(goto_line_input_, &parsed_chars);
        if (parsed_chars != goto_line_input_.size()) {
            active_action_ = "Invalid line number";
        } else {
            auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
            editor_ptr->JumpToLine(line_number);
            active_action_ = "Jumped to line " + std::to_string(line_number);
        }
    } catch (const std::exception&) {
        active_action_ = "Invalid line number";
    }

    show_goto_line_bar_ = false;
    FocusEditor();
}
    
    
    
bool TextltApp::SaveFile(const std::string& path, std::string& error) {
    try {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        const auto doc = editor_ptr->GetDocument();
        if (!file_manager_.SaveAs(doc, path, error)) {
            throw std::runtime_error(error);
        }
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
        git_manager_.Invalidate();
        active_action_ = "Saved " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}

bool TextltApp::OpenFile(const std::string& path, std::string& error) {
    try {
        PersistActiveFavoriteCursor();
        auto doc = file_manager_.Open(path, error);
        if (!doc) {
            throw std::runtime_error(error);
        }

        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->SetDocument(doc);

        RestoreFavoriteCursor(path);
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
        active_action_ = "Opened " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}

void TextltApp::RefreshFindMatches() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    if (find_query_.empty()) {
        active_action_ = "Find query empty";
    } else {
        active_action_ = FindMatchStatus();
    }
}

void TextltApp::FindNext() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    editor_ptr->JumpToNextMatch();
    active_action_ = FindMatchStatus();
}

void TextltApp::FindPrevious() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    editor_ptr->JumpToPreviousMatch();
    active_action_ = FindMatchStatus();
}

void TextltApp::ReplaceNext() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->ExecuteReplaceNext(find_query_, replace_text_);
    active_action_ = "Replace next: " + FindMatchStatus();
}

void TextltApp::ReplaceAll() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    editor_ptr->HighlightMatches(find_query_);
    const size_t count_before = editor_ptr->SearchMatchCount();
    editor_ptr->ExecuteReplaceAll(find_query_, replace_text_);
    active_action_ = "Replaced " + std::to_string(count_before) + " matches";
}

std::string TextltApp::FindMatchStatus() const {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const size_t count = editor_ptr->SearchMatchCount();
    if (find_query_.empty()) {
        return "Find";
    }
    if (count == 0) {
        return "No matches";
    }
    return "Match " + std::to_string(editor_ptr->CurrentSearchMatchIndex()) +
        " of " + std::to_string(count);
}


void TextltApp::InitializeWithFiles(const std::vector<std::string>& files_to_open) {
    if (files_to_open.empty()) {
        return;
    }

    const std::filesystem::path active_path = files_to_open.front();
    std::string error;
    if (std::filesystem::exists(active_path)) {
        OpenFile(active_path.string(), error);
    } else {
        std::static_pointer_cast<EditorComponent>(text_editor_)->NewFile(active_path.string());
        active_action_ = "New file " + active_path.string();
    }

    if (files_to_open.size() > 1) {
        active_action_ += " (" + std::to_string(files_to_open.size() - 1) +
            " additional path(s) ignored: tabs not implemented)";
    }

    FocusEditor();
}

void TextltApp::OpenSidebarFile(const std::filesystem::path& path) {
    std::string error;
    if (OpenFile(path.string(), error)) {
        FocusEditor();
        screen_.PostEvent(ftxui::Event::Custom);
    }
}

void TextltApp::SaveCurrentFile() {
    const std::string& current_path =
        std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
    if (current_path.empty() || current_path == "Untitled" || current_path == "untitled.txt") {
        OpenFileDialog(FilePromptMode::SaveAs);
        return;
    }

    std::string error;
    SaveFile(current_path, error);
}

bool TextltApp::ConfirmFileDialog(
    FilePromptMode mode,
    const std::string& path,
    std::string& error) {
    bool success = false;
    if (mode == FilePromptMode::Open) {
        success = OpenFile(path, error);
    } else if (mode == FilePromptMode::SaveAs) {
        success = SaveFile(path, error);
    } else {
        error = "No file action selected.";
        return false;
    }

    if (success && mode == FilePromptMode::SaveAs && exit_after_save_as_) {
        exit_after_save_as_ = false;
        PersistActiveFavoriteCursor();
        screen_.Exit();
    }
    return success;
}

void TextltApp::PreviewTheme(const std::string& theme_name) {
    current_theme_ = FindThemeByName(themes_, theme_name);
    editor_config_.SetActiveThemeName(current_theme_.name);
    active_action_ = "Previewing theme " + current_theme_.name;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SelectTheme(const std::string& theme_name) {
    current_theme_ = FindThemeByName(themes_, theme_name);
    editor_config_.SetActiveThemeName(current_theme_.name);
    active_action_ = "Theme changed to " + current_theme_.name;
    screen_.PostEvent(ftxui::Event::Custom);
    CloseThemeDialog();
}

void TextltApp::SaveConfig() {
    config_manager_.Save(editor_config_);
}

std::string TextltApp::ActiveDocumentFavoritePath() const {
    const auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    return EditorConfig::NormalizeFavoritePath(editor_ptr->CurrentFilePath());
}

void TextltApp::PersistActiveFavoriteCursor() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const std::string favorite_path =
        EditorConfig::NormalizeFavoritePath(editor_ptr->CurrentFilePath());
    if (favorite_path.empty() || !editor_config_.IsFavorite(favorite_path)) {
        return;
    }

    const size_t row = editor_ptr->GetCursorRow();
    const size_t column = editor_ptr->GetCursorCol();
    editor_config_.UpdateFavoriteCursor(favorite_path, row, column);
}

void TextltApp::RestoreFavoriteCursor(const std::string& path) {
    const FavoriteEntry* favorite = editor_config_.FindFavorite(path);
    if (!favorite) {
        return;
    }

    std::static_pointer_cast<EditorComponent>(text_editor_)
        ->SetCursorPosition(favorite->row, favorite->column);
}

void TextltApp::QueueCloudTtsDebugFromCursor() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

    // Submit document content and cursor row index for TTS processing
    cloud_tts_pipeline_.Submit(
        editor_ptr->GetAllText(),
                               editor_ptr->GetCursorRow());

    active_action_ = "Queued Cloud TTS debug pipeline";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ToggleActiveFavorite() {
    const std::string favorite_path = ActiveDocumentFavoritePath();
    if (favorite_path.empty()) {
        active_action_ = "Save the file before adding it to favorites";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (editor_config_.IsFavorite(favorite_path)) {
        editor_config_.RemoveFavorite(favorite_path);
        active_action_ = "Removed favorite " + favorite_path;
    } else {
        std::error_code error;
        if (!std::filesystem::is_regular_file(favorite_path, error)) {
            active_action_ = "Save the file before adding it to favorites";
            CloseDropdown();
            screen_.PostEvent(ftxui::Event::Custom);
            return;
        }
        editor_config_.AddFavorite(favorite_path);
        PersistActiveFavoriteCursor();
        active_action_ = "Added favorite " + favorite_path;
    }

    UpdateFileMenuLabels();
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::UpdateFileMenuLabels() {
    if (!menu_bar_) {
        return;
    }

    const std::string favorite_path = ActiveDocumentFavoritePath();
    menu_bar_->SetFileFavoriteLabel(editor_config_.IsFavorite(favorite_path));
}

void TextltApp::UpdateOptionsMenuLabels() {
    if (!menu_bar_) {
        return;
    }

    menu_bar_->SetOptionLabels(
        editor_config_.smart_word_wrap,
        editor_config_.syntax_highlighting,
        editor_config_.auto_pairing,
        editor_config_.auto_indent,
        editor_config_.tab_size);
}

void TextltApp::RunDropdownAction(int menu_index, int item_index) {
    // Generate simple trace actions for debugging inside the app status-bar
    active_action_ = "DEBUG: Menu=" + std::to_string(menu_index) +
                    " Item=" + std::to_string(item_index);

    // Sub-route requests explicitly based on the active dropdown catalog index
    switch (menu_index) {
        case 0: HandleFileMenu(item_index);     return;
        case 1: HandleEditMenu(item_index);     return;
        case 2: HandleOptionsMenu(item_index);  return;
        case 3:
            if (item_index == 0) {
                OpenAboutDialog();
            } else {
                OpenHelpDialog();
            }
            return;
        case 4: if (item_index == 0) RequestExit(); return;
        default: CloseDropdown();                            return;
    }
}
    
    void TextltApp::HandleFileMenu(int item) {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

        switch (item) {
            case 0: // New File
                PersistActiveFavoriteCursor();
                editor_ptr->NewFile("");
                active_action_ = "New file";
                screen_.PostEvent(ftxui::Event::Custom);
                break;

            case 1: OpenFileDialog(FilePromptMode::Open); return;
            case 2: SaveCurrentFile(); return;
            case 3: OpenFileDialog(FilePromptMode::SaveAs); return;
            case 4: ToggleActiveFavorite(); return;
            case 5: RequestExit(); return;

            default:
                CloseDropdown();
                return;
        }
        CloseDropdown();
        FocusEditor();
}

void TextltApp::HandleEditMenu(int item) {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);

    if (item == 0) { // Undo
        FocusEditor();
        editor_ptr->Undo();
        active_action_ = "Undo";
        CloseDropdown();
        return;
    }

    if (item == 1) { // Redo
        FocusEditor();
        editor_ptr->Redo();
        active_action_ = "Redo";
        CloseDropdown();
        return;
    }

    if (item == 2) { // Select All
        FocusEditor();
        editor_ptr->SelectAll();
        active_action_ = "Selected all text";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 3) { // Cut
        FocusEditor();
        if (editor_ptr->HasSelection()) {
            // Priority 1: Cut only the user's active Shift-selection
            std::string selected_text = editor_ptr->GetSelectedText();
            WriteSystemClipboard(selected_text);
            editor_ptr->DeleteSelection();
            active_action_ = "Cut selection to clipboard";
        } else {
            // Priority 2: Fallback to original behavior (cut entire line)
            std::string current_line = editor_ptr->GetCurrentLineText();
            if (!current_line.empty()) {
                WriteSystemClipboard(current_line);
                editor_ptr->DeleteCurrentLine();
                active_action_ = "Cut line to clipboard";
            } else {
                active_action_ = "Nothing to cut";
            }
        }
        CloseDropdown();
        return;
    }
    
    if (item == 4) { // Copy
        if (editor_ptr->HasSelection()) {
            // Priority 1: Copy only the active Shift-selection
            std::string selected_text = editor_ptr->GetSelectedText();
            WriteSystemClipboard(selected_text);
            active_action_ = "Copied selection to clipboard";
        } else {
            // Priority 2: Fallback to original behavior (copy entire line)
            std::string current_line = editor_ptr->GetCurrentLineText();
            if (!current_line.empty()) {
                WriteSystemClipboard(current_line);
                active_action_ = "Copied line to clipboard";
            } else {
                active_action_ = "Nothing to copy";
            }
        }
        CloseDropdown();
        return;
    }
    
    if (item == 5) { // Paste stream
        CloseDropdown();
        FocusEditor();

        std::string clipboard_text = ReadSystemClipboard();
        if (!clipboard_text.empty()) {
            active_action_ = "Pasted text (" + std::to_string(clipboard_text.size()) + " chars)";
            editor_ptr->InsertText(clipboard_text);
        } else {
            active_action_ = "Clipboard empty.";
        }
        return;
    }

    if (item == 6) { // Toggle Comment
        FocusEditor();
        editor_ptr->ToggleComment();
        active_action_ = "Toggle Comment";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 7) { // Toggle Case
        FocusEditor();
        editor_ptr->ToggleCase();
        active_action_ = "Toggle Case";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 8) { // Convert Indents: 4 -> 2 Spaces
        FocusEditor();
        editor_ptr->Convert4To2Spaces();
        active_action_ = "Converted leading indents from 4 to 2 spaces";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 9) { // Convert Indents: 2 -> 4 Spaces
        FocusEditor();
        editor_ptr->Convert2To4Spaces();
        active_action_ = "Converted leading indents from 2 to 4 spaces";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (item == 10) { // Find...
        CloseDropdown();
        OpenFindPanel(false);
        active_action_ = "Find";
        return;
    }

    if (item == 11) { // Replace...
        CloseDropdown();
        OpenFindPanel(true);
        active_action_ = "Replace";
        return;
    }

    CloseDropdown();
}

void TextltApp::HandleOptionsMenu(int item) {
    if (item == 0) {
        editor_config_.show_line_numbers = !editor_config_.show_line_numbers;
        active_action_ = editor_config_.show_line_numbers ? "Line numbers enabled" : "Line numbers disabled";
        SaveConfig();
    } else if (item == 1) {
        editor_config_.show_file_explorer = !editor_config_.show_file_explorer;
        active_action_ = editor_config_.show_file_explorer ? "File Explorer enabled" : "File Explorer disabled";
        SaveConfig();
    } else if (item == 2) {
        editor_config_.smart_word_wrap = !editor_config_.smart_word_wrap;
        active_action_ = editor_config_.smart_word_wrap ? "Smart Word Wrap enabled" : "Smart Word Wrap disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 3) {
        editor_config_.syntax_highlighting = !editor_config_.syntax_highlighting;
        active_action_ = editor_config_.syntax_highlighting ? "Syntax Highlighting enabled" : "Syntax Highlighting disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 4) {
        editor_config_.auto_pairing = !editor_config_.auto_pairing;
        active_action_ = editor_config_.auto_pairing ? "Auto Pairing enabled" : "Auto Pairing disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 5) {
        editor_config_.auto_indent = !editor_config_.auto_indent;
        active_action_ = editor_config_.auto_indent ? "Smart Auto-Indent enabled" : "Smart Auto-Indent disabled";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 6) {
        editor_config_.tab_size = editor_config_.tab_size == 2 ? 4 : 2;
        active_action_ = "Tab size set to " + std::to_string(editor_config_.tab_size) + " spaces";
        UpdateOptionsMenuLabels();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 7) {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
        editor_ptr->ConvertTabsToSpaces();
        active_action_ = "Converted tabs to spaces";
        screen_.PostEvent(ftxui::Event::Custom);
    } else if (item == 8) {
        OpenThemeDialog();
        return;
    }
    CloseDropdown();
}
    
    std::string TextltApp::ReadSystemClipboard() {
    std::string clipboard_text;
    char buffer[256];
    
    // Attempt 1: Standard X11 Clipboard via xclip
    FILE* pipe = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (pipe) {
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            clipboard_text += buffer;
        }
        pclose(pipe);
    }

    // Attempt 2: Fallback to xsel
    if (clipboard_text.empty()) {
        pipe = popen("xsel --clipboard --output 2>/dev/null", "r");
        if (pipe) {
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                clipboard_text += buffer;
            }
            pclose(pipe);
        }
    }

    // Attempt 3: Fallback to X11 Primary selection (mouse highlight)
    if (clipboard_text.empty()) {
        pipe = popen("xclip -selection primary -o 2>/dev/null", "r");
        if (pipe) {
            while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                clipboard_text += buffer;
            }
            pclose(pipe);
        }
    }

    return clipboard_text;
}

void TextltApp::WriteSystemClipboard(const std::string& text) {
    if (text.empty()) return;

    if (IsWslEnvironment() &&
        CommandAvailable("clip.exe") &&
        WriteTextToPipe("clip.exe 2>/dev/null", text)) {
        return;
    }

    WriteTextToPipe("xclip -selection clipboard -i 2>/dev/null", text);
}

} // namespace textlt
