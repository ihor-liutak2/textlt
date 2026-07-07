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

std::string TruncateLabel(const std::string& value, size_t max_length) {
    if (value.size() <= max_length) {
        return value;
    }
    if (max_length <= 3) {
        return value.substr(0, max_length);
    }
    return value.substr(0, max_length - 3) + "...";
}

ftxui::Color MainWindowSeparatorColor(const Theme& theme) {
    if (IsLightTheme(theme)) {
        return ftxui::Color::RGB(70, 70, 70);
    }
    return theme.gutter;
}


} // namespace

ftxui::Element TextltApp::Render() {
    using namespace ftxui;

    const auto editor = std::static_pointer_cast<EditorComponent>(text_editor_);
    UpdateFileMenuLabels();
    RefreshOpenedDocumentsSidebar();

    Element top_menu_element = menu_bar_->Render();
        
    Element editor_workspace = editor_workspace_container_
        ? editor_workspace_container_->Render() | xflex
        : text_editor_->Render() | xflex;

    Element workspace = editor_config_.show_file_explorer
        ? hbox({
              sidebar_panel_->Render() | size(WIDTH, EQUAL, 28),
              separator() | color(MainWindowSeparatorColor(current_theme_)),
              editor_workspace | xflex,
          })
        : hbox({
              editor_workspace | xflex,
          });

    Element top_menu_separator = separator() | color(MainWindowSeparatorColor(current_theme_));
    Elements base_rows = {
        top_bar_row_ ? top_bar_row_->Render() : text(" textlt v1.0.0 - Native Non-Modal Text Editor"),
        top_menu_separator,
        top_menu_element,
        separator() | color(MainWindowSeparatorColor(current_theme_)),
        workspace | yflex,
    };

    if (current_search_mode_ != SearchMode::None) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(RenderFindPanel());
    }
    if (show_goto_line_bar_) {
        base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
        base_rows.push_back(RenderGoToLinePanel());
    }

    base_rows.push_back(separator() | color(MainWindowSeparatorColor(current_theme_)));
    const int cursor_row_index = editor->GetCursorRow();
    const int cursor_row = cursor_row_index + 1;
    const int cursor_col = editor->GetCursorCol() + 1;
    const size_t total_lines = editor->GetLineCount();
    int document_percent = 100;
    if (total_lines > 1) {
        document_percent =
            (cursor_row_index * 100) / static_cast<int>(total_lines - 1);
    }
    const std::string file_path = editor->CurrentFilePath();
    const auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    const std::filesystem::path git_workspace = editor_config_.show_file_explorer
        ? sidebar->CurrentPath()
        : std::filesystem::path(file_path).parent_path();
    git_manager_.SetWorkingDirectory(
        git_workspace.empty() ? std::filesystem::current_path() : git_workspace);
    const std::string git_branch = git_manager_.GetCurrentBranch();
    const std::string git_branch_badge =
        git_branch.empty() ? "" : " | branch: " + TruncateLabel(git_branch, 15);

    auto doc = editor->GetSession();

    base_rows.push_back(
        text(" Ln " + std::to_string(cursor_row) + ", Col " + std::to_string(cursor_col) +
        " | " + editor->ActiveLineEndingLabel() +
        git_branch_badge +
        " | " + std::to_string(document_percent) + "%" +
        " | Theme: " + current_theme_.name) |
        color(current_theme_.menu_foreground));

    Element base_layout = vbox(std::move(base_rows)) |
        bgcolor(current_theme_.background);

    Elements layers = {base_layout};
    if (menu_bar_->IsDropdownOpen()) {
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
    if (ai_actions_modal_.IsOpen()) {
        layers.push_back(ai_actions_modal_.View()->Render() | clear_under | center);
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
        case UiLayer::AiActions: return ai_actions_modal_.IsOpen();
        case UiLayer::AssistantSettings: return assistant_settings_modal_.IsOpen();
    }
    return false;
}

bool TextltApp::HandleGlobalEvent(ftxui::Event event) {
    return app_event_dispatcher_.Handle(std::move(event));
}

} // namespace textlt
