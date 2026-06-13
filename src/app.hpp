#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "config_manager.hpp"
#include "editor_component.hpp"
#include "editor_config.hpp"
#include "file_dialog.hpp"
#include "file_explorer.hpp"
#include "help_dialog.hpp"
#include "theme_dialog.hpp"

namespace textlt {

class TextltApp {
public:
    TextltApp();
    void Run();

private:
    void CloseDropdown();
    void OpenDropdown();
    void CloseFileDialog();
    void OpenFileDialog(FilePromptMode mode);
    void OpenHelpDialog();
    void CloseHelpDialog();
    void OpenThemeDialog();
    void CloseThemeDialog();
    void ActivateTopMenu();
    void SwitchEditorFocus();
    void FocusEditor();
    void FocusExplorer();
    bool SaveFile(const std::string& path, std::string& error);
    bool OpenFile(const std::string& path, std::string& error);
    void OpenExplorerFile(const std::filesystem::path& path);
    void SaveCurrentFile();
    bool ConfirmFileDialog(FilePromptMode mode, const std::string& path, std::string& error);
    void SelectTheme(const std::string& theme_name);
    void SaveConfig();
    void RunDropdownAction();
    ftxui::Element Render();
    ftxui::Element RenderFindPanel();
    bool HandleGlobalEvent(ftxui::Event event);
    int DropdownX() const;
    void OpenFindPanel(bool replace_mode);
    void CloseFindPanel();
    void RefreshFindMatches();
    void FindNext();
    void FindPrevious();
    void ReplaceNext();
    void ReplaceAll();
    std::string FindMatchStatus() const;
    
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
    ftxui::Component text_editor_;
    ftxui::Component file_explorer_;
    FileDialog file_dialog_;
    HelpDialog help_dialog_;
    ThemeDialog theme_dialog_;

    std::vector<std::string> menu_entries_;
    std::vector<std::vector<std::string>> dropdown_entries_;
    std::vector<std::string> current_dropdown_entries_;
    std::string find_query_;
    std::string replace_text_;
    std::string active_action_ = "Ready";

    int selected_menu_item_ = 0;
    int active_dropdown_ = -1;
    int focused_layer_ = 0;
    int selected_dropdown_item_ = 0;
    int find_panel_mode_index_ = 0;
    bool explorer_has_focus_ = false;
    bool find_panel_active_ = false;
    bool replace_panel_mode_ = false;
    ftxui::Box top_menu_box_;

    ftxui::Component top_menu_;
    ftxui::Component dropdown_menu_;
    ftxui::Component body_container_;
    ftxui::Component main_container_;
    ftxui::Component root_container_;
    ftxui::Component find_input_;
    ftxui::Component replace_input_;
    ftxui::Component replace_next_button_;
    ftxui::Component replace_all_button_;
    ftxui::Component find_panel_find_container_;
    ftxui::Component find_panel_replace_container_;
    ftxui::Component find_panel_container_;
    ftxui::Component renderer_;
    ftxui::Component global_shortcuts_;
};

} // namespace textlt
