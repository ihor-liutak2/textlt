#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include <ftxui/component/component_base.hpp>
#include "file_utils.hpp"
#include "keyboard_shortcuts.hpp"
#include "theme.hpp"

using namespace ftxui;

namespace textlt {

namespace {

bool IsLightTheme(const Theme& theme) {
    return theme.name.find("Light") != std::string::npos;
}

ftxui::Color MainWindowSeparatorColor(const Theme& theme) {
    if (IsLightTheme(theme)) {
        return (ftxui::Color::RGB)(70, 70, 70);
    }
    return theme.gutter;
}


} // namespace

BottomBarRowState TextltApp::CurrentBottomBarRowState() {
    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);

    BottomBarRowState state;
    state.show_cursor_position = workspace_mode_ == WorkspaceMode::Documents;
    const size_t cursor_row_index = editor ? editor->GetCursorRow() : 0;
    state.cursor_row = cursor_row_index + 1;
    state.cursor_col = editor ? editor->GetCursorCol() + 1 : 1;
    state.total_lines = editor ? editor->GetLineCount() : 1;
    state.document_percent = 100;
    if (state.total_lines > 1) {
        state.document_percent = static_cast<int>(
            (cursor_row_index * 100) / (state.total_lines - 1));
    }
    state.git_branch = git_manager_.GetCurrentBranch();
    state.theme_name = current_theme_.name;

    if (workspace_mode_ == WorkspaceMode::Notes) {
        state.workspace_label = "Notes";
    } else if (workspace_mode_ == WorkspaceMode::Actions) {
        state.workspace_label = "Action";
    } else {
        state.workspace_label = "Docs";
        state.line_ending = editor ? editor->ActiveLineEndingLabel() : "LF";
    }

    if (const ProjectRecord* project = ActiveWorkspaceProject()) {
        state.project_name = project->name;
    }

    return state;
}

ftxui::Element TextltApp::Render() {
    using namespace ftxui;


    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    const bool distraction_mode = layout_controller_.IsDistractionModeActive();
    SyncFindPanelTarget();
    if (find_panel_open_ && find_replace_panel_) {
        find_replace_panel_->Tick();
    }
    const bool find_open = find_panel_open_ && find_replace_panel_;
    const bool show_sidebar = !find_open && !distraction_mode &&
        ((workspace_mode_ == WorkspaceMode::Documents && editor_config_.show_file_explorer) ||
         (workspace_mode_ == WorkspaceMode::Actions && editor_config_.show_actions_sidebar));
    if (!show_sidebar) {
        std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->ClearHitboxes();
    }
    Element find_panel_element = find_open
        ? find_replace_panel_->Render() | size(WIDTH, EQUAL, 28) | yflex
        : emptyElement();
    UpdateFileMenuLabels();
    UpdateNotesMenuLabels();
    RefreshOpenedDocumentsSidebar();

    Element top_menu_element = distraction_mode ? emptyElement() : menu_bar_->Render();
        
    Element editor_workspace = editor_workspace_container_
        ? editor_workspace_container_->Render() | xflex
        : text_editor_->Render() | xflex;

    Element workspace = workspace_mode_ == WorkspaceMode::Notes && notes_workspace_component_
        ? (find_open
              ? hbox({find_panel_element, text("  "),
                      notes_workspace_component_->Render() | xflex}) | flex
              : notes_workspace_component_->Render() | flex)
        : workspace_mode_ == WorkspaceMode::Actions && actions_workspace_component_
        ? (find_open
              ? hbox({find_panel_element, text("  "),
                      actions_workspace_component_->Render() | xflex})
              : show_sidebar
              ? hbox({sidebar_panel_->Render() | size(WIDTH, EQUAL, 28),
                      text("  "),
                      actions_workspace_component_->Render() | xflex})
              : hbox({actions_workspace_component_->Render() | xflex}))
        : find_open
        ? hbox({
              find_panel_element,
              text("  "),
              editor_workspace | xflex,
          })
        : show_sidebar
        ? hbox({
              sidebar_panel_->Render() | size(WIDTH, EQUAL, 28),
              text("  "),
              editor_workspace | xflex,
          })
        : hbox({
              editor_workspace | xflex,
          });

    Elements base_rows = {
        top_bar_row_ ? top_bar_row_->Render()
                     : text(" textlt v" TEXTLT_VERSION " - Native Non-Modal Text Editor"),
    };

    if (!distraction_mode) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(top_menu_element);
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
    }

    base_rows.push_back(workspace | yflex);

    if (!distraction_mode) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(
            bottom_bar_row_
                ? bottom_bar_row_->Render()
                : text(" Ln 1, Col 1 | Docs | LF | 100% | Theme: " + current_theme_.name) |
                      color(current_theme_.menu_foreground));
    }

    Element base_layout = vbox(std::move(base_rows)) |
        bgcolor(current_theme_.background);

    Elements layers = {base_layout};
    if (show_goto_line_bar_) {
        layers.push_back(RenderGoToLinePanel() | clear_under | center);
    }
    if (ActiveLayer() == UiLayer::RenameProject) {
        layers.push_back(RenderRenameProjectPanel() | clear_under | center);
    }
    if (!distraction_mode && menu_bar_->IsDropdownOpen()) {
        layers.push_back(menu_bar_->RenderDropdown());
    }

    if (help_dialog_.IsOpen()) {
        layers.push_back(help_dialog_.View()->Render() | clear_under | center);
    }
    if (keyboard_shortcuts_modal_.IsOpen()) {
        layers.push_back(keyboard_shortcuts_modal_.View()->Render() | clear_under | center);
    }
    if (sidebar_shortcuts_modal_.IsOpen()) {
        layers.push_back(sidebar_shortcuts_modal_.View()->Render() | clear_under | center);
    }
    if (custom_processor_builder_modal_.IsOpen()) {
        layers.push_back(custom_processor_builder_modal_.View()->Render() | clear_under | center);
    }
    if (recent_files_modal_.IsOpen()) {
        layers.push_back(recent_files_modal_.View()->Render() | clear_under | center);
    }
    if (search_files_modal_.IsOpen()) {
        layers.push_back(search_files_modal_.View()->Render() | clear_under | center);
    }
    if (files_modal_.IsOpen()) {
        layers.push_back(files_modal_.View()->Render() | clear_under | center);
    }
    if (text_processors_modal_.IsOpen()) {
        layers.push_back(text_processors_modal_.View()->Render() | clear_under | center);
    }
    if (remote_connections_modal_.IsOpen()) {
        layers.push_back(remote_connections_modal_.View()->Render() | clear_under | center);
    }
    if (remote_files_modal_.IsOpen()) {
        layers.push_back(remote_files_modal_.View()->Render() | clear_under | center);
    }
    if (git_modal_.IsOpen()) {
        layers.push_back(git_modal_.View()->Render() | clear_under | center);
    }
    if (git_settings_modal_.IsOpen()) {
        layers.push_back(git_settings_modal_.View()->Render() | clear_under | center);
    }
    if (theme_dialog_.IsOpen()) {
        layers.push_back(theme_dialog_.View()->Render() | clear_under | center);
    }
    if (theme_colors_modal_.IsOpen()) {
        layers.push_back(theme_colors_modal_.View()->Render() | clear_under | center);
    }
    if (tts_modal_.IsOpen()) {
        layers.push_back(tts_modal_.View()->Render() | clear_under | center);
    }
    if (view_layout_modal_.IsOpen()) {
        layers.push_back(view_layout_modal_.View()->Render() | clear_under | center);
    }
    if (distraction_options_modal_.IsOpen()) {
        layers.push_back(distraction_options_modal_.View()->Render() | clear_under | center);
    }
    if (quick_ai_modal_.IsOpen()) {
        layers.push_back(quick_ai_modal_.View()->Render() | clear_under | center);
    }
    if (ai_settings_modal_.IsOpen()) {
        layers.push_back(ai_settings_modal_.View()->Render() | clear_under | center);
    }
    if (assistant_settings_modal_.IsOpen()) {
        layers.push_back(assistant_settings_modal_.View()->Render() | clear_under | center);
    }
    if (unsaved_changes_dialog_.IsOpen()) {
        layers.push_back(unsaved_changes_dialog_.View()->Render() | clear_under | center);
    }

    return dbox(std::move(layers));
}

ftxui::Element TextltApp::RenderGoToLinePanel() {
    using namespace ftxui;

    const bool page_mode = navigation_popup_mode_ == NavigationPopupMode::GoToPage;
    const size_t maximum_value = page_mode
        ? std::max<size_t>(CurrentDistractionTopBarState().total_pages, 1)
        : std::max<size_t>(
              std::static_pointer_cast<EditorComponent>(text_editor_)->GetLineCount(),
              1);
    const std::string title = page_mode ? " Goto page " : " Goto line ";
    const std::string label = page_mode ? " Page " : " Line ";

    const Element input = goto_line_input_component_
        ? goto_line_input_component_->Render() |
              size(WIDTH, EQUAL, 14) |
              borderStyled(LIGHT, current_theme_.modal_border)
        : text("") | size(WIDTH, EQUAL, 14);

    return vbox({
        text(title) | bold | color(current_theme_.modal_accent),
        hbox({
            vbox({text(""), text(label) | color(current_theme_.modal_text_color)}),
            input,
            vbox({text(""), text(" / " + std::to_string(maximum_value) + "  ") | color(current_theme_.modal_text_color)}),
            vbox({text(""), goto_line_go_button_ ? goto_line_go_button_->Render() : text("Go")}),
            text(" "),
            vbox({text(""), goto_line_cancel_button_ ? goto_line_cancel_button_->Render() : text("Cancel")}),
        }),
    }) |
        borderStyled(LIGHT, current_theme_.modal_border) |
        bgcolor(current_theme_.modal_background) |
        color(current_theme_.modal_text_color);
}

ftxui::Element TextltApp::RenderRenameProjectPanel() {
    using namespace ftxui;
    Elements rows = {
        text(" Rename project ") | bold | color(current_theme_.modal_accent),
        project_name_input_->Render() | borderStyled(LIGHT, current_theme_.modal_border),
    };
    if (!rename_project_error_.empty()) {
        rows.push_back(paragraph(rename_project_error_) | color(current_theme_.modal_text_color));
    }
    rows.push_back(hbox({
        filler(),
        rename_project_button_->Render(),
        text(" "),
        cancel_rename_project_button_->Render(),
    }));
    return vbox(std::move(rows)) | size(WIDTH, EQUAL, 48) |
        borderStyled(LIGHT, current_theme_.modal_border) |
        bgcolor(current_theme_.modal_background);
}

void TextltApp::SetActiveLayer(UiLayer layer) {
    active_layer_ = layer;
    active_layer_index_ = static_cast<int>(layer);
}

TextltApp::UiLayer TextltApp::ActiveLayer() const {
    return active_layer_;
}

bool TextltApp::ActiveModalIsOpen() const {
    switch (ActiveLayer()) {
        case UiLayer::Main: return true;
        case UiLayer::Help: return help_dialog_.IsOpen();
        case UiLayer::KeyboardShortcuts: return keyboard_shortcuts_modal_.IsOpen();
        case UiLayer::SidebarShortcuts: return sidebar_shortcuts_modal_.IsOpen();
        case UiLayer::CustomProcessorBuilder: return custom_processor_builder_modal_.IsOpen();
        case UiLayer::Theme: return theme_dialog_.IsOpen();
        case UiLayer::ThemeColors: return theme_colors_modal_.IsOpen();
        case UiLayer::GoToLine: return show_goto_line_bar_;
        case UiLayer::UnsavedChanges: return unsaved_changes_dialog_.IsOpen();
        case UiLayer::RecentFiles: return recent_files_modal_.IsOpen();
        case UiLayer::SearchFiles: return search_files_modal_.IsOpen();
        case UiLayer::Files: return files_modal_.IsOpen();
        case UiLayer::TextProcessors: return text_processors_modal_.IsOpen();
        case UiLayer::RemoteConnections: return remote_connections_modal_.IsOpen();
        case UiLayer::RemoteFiles: return remote_files_modal_.IsOpen();
        case UiLayer::Git: return git_modal_.IsOpen();
        case UiLayer::GitSettings: return git_settings_modal_.IsOpen();
        case UiLayer::Tts: return tts_modal_.IsOpen();
        case UiLayer::ViewLayout: return view_layout_modal_.IsOpen();
        case UiLayer::DistractionOptions: return distraction_options_modal_.IsOpen();
        case UiLayer::QuickAi: return quick_ai_modal_.IsOpen();
        case UiLayer::AiSettings: return ai_settings_modal_.IsOpen();
        case UiLayer::AssistantSettings: return assistant_settings_modal_.IsOpen();
        case UiLayer::RenameProject: return !rename_project_id_.empty();
    }
    return false;
}

bool TextltApp::HandleGlobalEvent(ftxui::Event event) {
    return app_event_dispatcher_.Handle(std::move(event));
}

} // namespace textlt
