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
        return ftxui::Color::RGB(70, 70, 70);
    }
    return theme.gutter;
}


} // namespace

BottomBarRowState TextltApp::CurrentBottomBarRowState() {
    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);

    BottomBarRowState state;
    const int cursor_row_index = editor ? editor->GetCursorRow() : 0;
    state.cursor_row = cursor_row_index + 1;
    state.cursor_col = editor ? editor->GetCursorCol() + 1 : 1;
    state.total_lines = editor ? editor->GetLineCount() : 1;
    state.document_percent = 100;
    if (state.total_lines > 1) {
        state.document_percent =
            (cursor_row_index * 100) / static_cast<int>(state.total_lines - 1);
    }
    state.line_ending = editor ? editor->ActiveLineEndingLabel() : "LF";
    state.theme_name = current_theme_.name;

    const std::string file_path = editor ? editor->CurrentFilePath() : std::string();
    const auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    const std::filesystem::path git_workspace = editor_config_.show_file_explorer && sidebar
        ? sidebar->CurrentPath()
        : std::filesystem::path(file_path).parent_path();
    git_manager_.SetWorkingDirectory(
        git_workspace.empty() ? std::filesystem::current_path() : git_workspace);
    state.git_branch = git_manager_.GetCurrentBranch();

    return state;
}

ftxui::Element TextltApp::Render() {
    using namespace ftxui;

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    const bool distraction_mode = layout_controller_.IsDistractionModeActive();
    UpdateFileMenuLabels();
    RefreshOpenedDocumentsSidebar();

    Element top_menu_element = distraction_mode ? emptyElement() : menu_bar_->Render();
        
    Element editor_workspace = editor_workspace_container_
        ? editor_workspace_container_->Render() | xflex
        : text_editor_->Render() | xflex;

    Element workspace = !distraction_mode && editor_config_.show_file_explorer
        ? hbox({
              sidebar_panel_->Render() | size(WIDTH, EQUAL, 28),
              separator() | color(MainWindowSeparatorColor(current_theme_)),
              editor_workspace | xflex,
          })
        : hbox({
              editor_workspace | xflex,
          });

    Elements base_rows = {
        top_bar_row_ ? top_bar_row_->Render() : text(" textlt v1.0.0 - Native Non-Modal Text Editor"),
    };

    if (!distraction_mode) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(top_menu_element);
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
    }

    base_rows.push_back(workspace | yflex);

    if (current_search_mode_ != SearchMode::None) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(RenderFindPanel());
    }
    if (show_goto_line_bar_) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(RenderGoToLinePanel());
    }

    if (!distraction_mode) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(
            bottom_bar_row_
                ? bottom_bar_row_->Render()
                : text(" Ln 1, Col 1 | LF | 100% | Theme: " + current_theme_.name) |
                      color(current_theme_.menu_foreground));
    }

    Element base_layout = vbox(std::move(base_rows)) |
        bgcolor(current_theme_.background);

    Elements layers = {base_layout};
    if (!distraction_mode && menu_bar_->IsDropdownOpen()) {
        layers.push_back(menu_bar_->RenderDropdown());
    }

    if (help_dialog_.IsOpen()) {
        layers.push_back(help_dialog_.View()->Render() | clear_under | center);
    }
    if (keyboard_shortcuts_modal_.IsOpen()) {
        layers.push_back(keyboard_shortcuts_modal_.View()->Render() | clear_under | center);
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
    if (tts_modal_.IsOpen()) {
        layers.push_back(tts_modal_.View()->Render() | clear_under | center);
    }
    if (view_layout_modal_.IsOpen()) {
        layers.push_back(view_layout_modal_.View()->Render() | clear_under | center);
    }
    if (distraction_options_modal_.IsOpen()) {
        layers.push_back(distraction_options_modal_.View()->Render() | clear_under | center);
    }
    if (ai_actions_modal_.IsOpen()) {
        layers.push_back(ai_actions_modal_.View()->Render() | clear_under | center);
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

ftxui::Element TextltApp::RenderFindPanel() {
    using namespace ftxui;

    Element input_column = emptyElement();
    Element utility_column = emptyElement();
    Element action_column = emptyElement();
    std::string mode = "Find";
    if (current_search_mode_ == SearchMode::Replace) {
        mode = "Replace";
        input_column = vbox({
            hbox({
                text(" Find:    "),
                replace_find_input_->Render() | size(WIDTH, GREATER_THAN, 34) | xflex,
            }),
            emptyElement(),
            hbox({
                text(" Replace: "),
                replace_input_->Render() | size(WIDTH, GREATER_THAN, 34) | xflex,
            }),
        });
        utility_column = vbox({
            replace_paste_button_->Render(),
            replace_clear_button_->Render(),
        });
        action_column = vbox({
            replace_next_button_->Render(),
            replace_all_button_->Render(),
        });
    } else {
        input_column = vbox({
            hbox({
                text(" Find:    "),
                find_input_->Render() | size(WIDTH, GREATER_THAN, 42) | xflex,
            }),
        });
        utility_column = vbox({
            find_paste_button_->Render(),
            find_clear_button_->Render(),
        });
        action_column = vbox({
            find_next_button_->Render(),
            find_previous_button_->Render(),
        });
    }

    // The search panel is split into three columns so text fields keep most
    // horizontal space while actions and filters remain readable at small widths.
    Element filter_column = vbox({
        search_match_case_checkbox_->Render(),
        search_whole_word_checkbox_->Render(),
    });

    return vbox({
        hbox({
            text(" " + mode + " ") | bold | color(current_theme_.menu_foreground),
            separator() | color(MainWindowSeparatorColor(current_theme_)),
            text(" " + FindMatchStatus() + " ") | color(current_theme_.menu_foreground),
            filler(),
            text(" Esc closes ") | dim | color(current_theme_.foreground),
        }),
        hbox({
            input_column | xflex,
            separator() | color(MainWindowSeparatorColor(current_theme_)),
            utility_column | size(WIDTH, GREATER_THAN, 9),
            text(" "),
            action_column | size(WIDTH, GREATER_THAN, 16),
            separator() | color(MainWindowSeparatorColor(current_theme_)),
            filter_column | size(WIDTH, GREATER_THAN, 18),
        }),
    }) |
        border |
        bgcolor(current_theme_.menu_background) |
        color(current_theme_.menu_foreground);
}

ftxui::Element TextltApp::RenderGoToLinePanel() {
    using namespace ftxui;

    return hbox({
        text(" Go to Line: ") | bold | color(current_theme_.menu_foreground),
        goto_line_input_component_->Render() | size(WIDTH, GREATER_THAN, 12) | xflex,
        separator() | color(MainWindowSeparatorColor(current_theme_)),
        text(" Enter jumps | Esc closes ") | dim | color(current_theme_.foreground),
    }) |
        border |
        bgcolor(current_theme_.menu_background) |
        color(current_theme_.menu_foreground);
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
        case UiLayer::CustomProcessorBuilder: return custom_processor_builder_modal_.IsOpen();
        case UiLayer::Theme: return theme_dialog_.IsOpen();
        case UiLayer::Find: return current_search_mode_ != SearchMode::None;
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
        case UiLayer::AiActions: return ai_actions_modal_.IsOpen();
        case UiLayer::AiSettings: return ai_settings_modal_.IsOpen();
        case UiLayer::AssistantSettings: return assistant_settings_modal_.IsOpen();
    }
    return false;
}

bool TextltApp::HandleGlobalEvent(ftxui::Event event) {
    return app_event_dispatcher_.Handle(std::move(event));
}

} // namespace textlt
