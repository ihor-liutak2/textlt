#include "app_event_dispatcher.hpp"

#include "app.hpp"

#include "connect_workspace.hpp"

#include <memory>
#include <string>

#include "ftxui/component/mouse.hpp"
#include "keyboard_shortcuts.hpp"

namespace textlt {

namespace {

bool IsAltLeftShortcut(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Alt+Left" ||
        input == "\x1B[1;3D" ||
        input == "\x1B[1;9D" ||
        input == "\x1B[27;3;68~" ||
        input == "\x1B[68;3u" ||
        event == ftxui::Event::Special("Alt+Left");
}

bool IsAltRightShortcut(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "Alt+Right" ||
        input == "\x1B[1;3C" ||
        input == "\x1B[1;9C" ||
        input == "\x1B[27;3;67~" ||
        input == "\x1B[67;3u" ||
        event == ftxui::Event::Special("Alt+Right");
}

bool IsPrimaryMousePress(ftxui::Event event) {
    return event.is_mouse() &&
        event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed;
}

bool IsMouseWheelUp(ftxui::Event event) {
    return event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelUp;
}

bool IsMouseWheelDown(ftxui::Event event) {
    return event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelDown;
}

} // namespace

AppEventDispatcher::AppEventDispatcher(TextltApp& app) : app_(app) {}

bool AppEventDispatcher::Handle(ftxui::Event event) {
    RestoreClosedModalFocus();

    if (HandleActiveModalEvent(event)) {
        return true;
    }

    if (app_.ActiveLayer() != TextltApp::UiLayer::Main) {
        return false;
    }

    return HandleMainEvent(event);
}

bool AppEventDispatcher::HandleBodyEvent(ftxui::Event event) {
    if (IsPrimaryMousePress(event) && app_.ActiveLayer() == TextltApp::UiLayer::Main) {
        for (size_t pane_index = 0; pane_index < app_.VisibleEditorPaneCount() &&
             pane_index < app_.editor_pane_components_.size(); ++pane_index) {
            auto editor = std::static_pointer_cast<EditorComponent>(app_.editor_pane_components_[pane_index]);
            const auto doc = editor ? editor->GetSession() : nullptr;
            if (!doc || !doc->git_compare) continue;
            const int x = event.mouse().x;
            const int y = event.mouse().y;
            const auto& previous = app_.git_compare_prev_boxes_[pane_index];
            const auto& next = app_.git_compare_next_boxes_[pane_index];
            if (previous.x_max > previous.x_min && previous.y_max >= previous.y_min &&
                previous.Contain(x, y)) {
                app_.NavigateGitCompare(pane_index, false);
                return true;
            }
            if (next.x_max > next.x_min && next.y_max >= next.y_min && next.Contain(x, y)) {
                app_.NavigateGitCompare(pane_index, true);
                return true;
            }
        }
    }

    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Notes ||
        app_.workspace_mode_ == TextltApp::WorkspaceMode::Connect) {
        return false;
    }
    if (IsPrimaryMousePress(event) && app_.ActiveLayer() == TextltApp::UiLayer::Main) {
        auto sidebar = std::static_pointer_cast<SidebarPanel>(app_.sidebar_panel_);
        const bool distraction_mode = app_.layout_controller_.IsDistractionModeActive();
        const bool sidebar_visible =
            app_.workspace_mode_ == TextltApp::WorkspaceMode::Actions
                ? app_.editor_config_.show_actions_sidebar
                : app_.editor_config_.show_file_explorer;
        const bool inside_sidebar = !distraction_mode &&
            sidebar_visible &&
            sidebar &&
            sidebar->ContainsPoint(event.mouse().x, event.mouse().y);
        if (!distraction_mode && inside_sidebar) {
            app_.FocusSidebar();
        } else {
            app_.FocusEditor();
        }
    }

    if (event == ftxui::Event::Tab &&
        app_.ActiveLayer() == TextltApp::UiLayer::Main &&
        (!app_.menu_bar_ || !app_.menu_bar_->IsDropdownOpen()) &&
        !app_.help_dialog_.IsOpen() &&
        !app_.keyboard_shortcuts_modal_.IsOpen() &&
        !app_.custom_processor_builder_modal_.IsOpen() &&
        !app_.recent_files_modal_.IsOpen() &&
        !app_.search_files_modal_.IsOpen() &&
        !app_.files_modal_.IsOpen() &&
        !app_.text_processors_modal_.IsOpen() &&
        !app_.remote_connections_modal_.IsOpen() &&
        !app_.remote_files_modal_.IsOpen() &&
        !app_.git_modal_.IsOpen() &&
        !app_.git_settings_modal_.IsOpen() &&
        !app_.tts_modal_.IsOpen() &&
        !app_.view_layout_modal_.IsOpen() &&
        !app_.distraction_options_modal_.IsOpen() &&
        !app_.quick_ai_modal_.IsOpen() &&
        !app_.ai_settings_modal_.IsOpen() &&
        !app_.assistant_settings_modal_.IsOpen() &&
        !app_.theme_dialog_.IsOpen() &&
        !app_.sidebar_has_focus_) {
        app_.text_editor_->OnEvent(event);
        return true;
    }

    return false;
}

bool AppEventDispatcher::HandleEditorPaneEvent(size_t pane_index, ftxui::Event event) {
    if (IsPrimaryMousePress(event) && pane_index < app_.VisibleEditorPaneCount()) {
        auto editor = pane_index < app_.editor_pane_components_.size()
            ? std::static_pointer_cast<EditorComponent>(app_.editor_pane_components_[pane_index])
            : nullptr;
        const auto doc = editor ? editor->GetSession() : nullptr;
        if (doc && doc->git_compare && pane_index < app_.git_compare_prev_boxes_.size()) {
            const int x = event.mouse().x;
            const int y = event.mouse().y;
            if (app_.git_compare_prev_boxes_[pane_index].Contain(x, y)) {
                app_.NavigateGitCompare(pane_index, false);
                return true;
            }
            if (app_.git_compare_next_boxes_[pane_index].Contain(x, y)) {
                app_.NavigateGitCompare(pane_index, true);
                return true;
            }
        }
    }

    if (app_.layout_controller_.IsDistractionModeActive() &&
        pane_index < app_.VisibleEditorPaneCount() &&
        (IsMouseWheelUp(event) || IsMouseWheelDown(event))) {
        const EditorViewport* viewport = app_.document_workspace_.PaneViewport(pane_index);
        if (viewport && viewport->box.Contain(event.mouse().x, event.mouse().y)) {
            if (IsMouseWheelUp(event)) {
                app_.CommandDistractionPreviousPage();
            } else {
                app_.CommandDistractionNextPage();
            }
            return true;
        }
    }

    if (IsPrimaryMousePress(event) &&
        pane_index < app_.VisibleEditorPaneCount() &&
        app_.MainViewCanActivateEditorPane() &&
        !app_.sidebar_has_focus_) {
        const EditorViewport* viewport = app_.document_workspace_.PaneViewport(pane_index);
        if (!viewport || !viewport->box.Contain(event.mouse().x, event.mouse().y)) return false;
        app_.SetActiveEditorPane(pane_index);
        auto editor = std::static_pointer_cast<EditorComponent>(app_.editor_pane_components_[pane_index]);
        if (editor) {
            editor->TakeFocus();
        }
    }

    return false;
}

bool AppEventDispatcher::RestoreClosedModalFocus() {
    // Modal content is selected by Container::Tab and receives keyboard,
    // focus and mouse events through the FTXUI component tree. If a modal
    // closed itself from one of its controls, restore the main layer before
    // handling the next event.
    if (app_.ActiveLayer() != TextltApp::UiLayer::Main && !app_.ActiveModalIsOpen()) {
        app_.FocusEditor();
        return true;
    }
    return false;
}

bool AppEventDispatcher::HandleActiveModalEvent(const ftxui::Event& event) {
    if (app_.ActiveLayer() == TextltApp::UiLayer::KeyboardShortcuts) {
        if (event == ftxui::Event::Escape) {
            app_.CloseKeyboardShortcutsModal();
            return true;
        }
        return false;
    }

    if (app_.ActiveLayer() == TextltApp::UiLayer::CustomProcessorBuilder) {
        if (event == ftxui::Event::Escape) {
            app_.CloseCustomProcessorBuilderModal();
            return true;
        }
        return false;
    }

    if (app_.ActiveLayer() == TextltApp::UiLayer::GoToLine) {
        if (event == ftxui::Event::Escape) {
            app_.CloseGoToLinePanel();
            return true;
        }
        return false;
    }

    return false;
}

bool AppEventDispatcher::HandleMainEvent(const ftxui::Event& event) {
    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Actions &&
        !app_.menu_bar_->IsDropdownOpen()) {
        const auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(
            app_.actions_workspace_component_);
        if (workspace) {
            const bool already_open = workspace->HasOpenContextMenu();
            if (workspace->HandleContextMenuEvent(event)) {
                if (!already_open && workspace->HasOpenContextMenu()) {
                    app_.sidebar_has_focus_ = false;
                    app_.find_panel_focus_ = false;
                    if (app_.find_replace_panel_) app_.find_replace_panel_->SetFocused(false);
                }
                return true;
            }
        }
    }

    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Notes) {
        const auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(
            app_.notes_workspace_component_);
        if (notes && notes->HasOpenContextMenu() && notes->HandleContextMenuEvent(event)) return true;
    }

    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Documents) {
        if (app_.editor_context_menu_.OnEvent(event)) return true;
        if (event.is_mouse()) {
            auto mouse_event = event;
            const auto& mouse = mouse_event.mouse();
            if (mouse.button == ftxui::Mouse::Right && mouse.motion == ftxui::Mouse::Pressed &&
                app_.OpenEditorContextMenu(mouse.x, mouse.y)) {
                return true;
            }
        }
    }

    // Let FTXUI route menu events through the component tree. Calling
    // MenuBarComponent::OnEvent directly from this outer CatchEvent changes
    // layers in the middle of a mouse sequence when an item opens a modal.
    if (app_.layout_controller_.IsDistractionModeActive() && app_.menu_bar_->IsDropdownOpen()) {
        app_.menu_bar_->CloseDropdown();
        return true;
    }

    if (app_.menu_bar_->IsDropdownOpen()) {
        return event.is_mouse() ? false : app_.menu_bar_->OnEvent(event);
    }

    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Connect) {
        if (app_.connect_workspace_->HandleMessageMenuEvent(event)) return true;
        if (app_.connect_workspace_->Paste(event)) return true;
        if (event.is_mouse()) return false;
        return app_.RunMenuShortcut(event) || HandleFunctionKeyEvent(event);
    }

    if (app_.find_panel_open_ && app_.find_replace_panel_) {
        if (event == ftxui::Event::Escape) {
            app_.CloseFindPanel();
            return true;
        }
        if (event.is_mouse()) {
            // Event::mouse() is non-const, so inspect a copy of this event.
            ftxui::Event mouse_event = event;
            const auto& mouse = mouse_event.mouse();
            if (app_.find_replace_panel_->Contains(mouse.x, mouse.y)) {
                app_.find_panel_focus_ = true;
                app_.find_replace_panel_->SetFocused(true);
                app_.find_replace_panel_->OnEvent(mouse_event);
                if (event != ftxui::Event::Custom) {
                    app_.screen_.PostEvent(ftxui::Event::Custom);
                }
                return true;
            }
            if (IsPrimaryMousePress(event)) {
                app_.find_panel_focus_ = false;
                app_.find_replace_panel_->SetFocused(false);
            }
        } else if (event != ftxui::Event::Custom && app_.find_panel_focus_) {
            if (app_.find_replace_panel_->OnEvent(event)) {
                app_.screen_.PostEvent(ftxui::Event::Custom);
                return true;
            }
        }
    }

    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Documents) {
        if (const auto text = ClipboardController::PastedText(event)) {
            if (!app_.find_panel_focus_) {
                auto editor = std::static_pointer_cast<EditorComponent>(app_.text_editor_);
                app_.FocusEditor();
                app_.active_action_ = ClipboardController::PasteIntoEditor(*editor, *text).message;
            }
            return true;
        }
    }

    if (!event.is_mouse() && app_.shortcut_registry_.MatchAction(ShortcutContext::Menu, event) == "ai.quick") {
        return app_.RunCommand("ai.quick");
    }

    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Notes) {
        if (app_.notes_workspace_component_ && app_.notes_workspace_component_->OnEvent(event)) {
            // Do not answer a redraw request with another redraw request:
            // components that swallow every event would otherwise keep the
            // event queue busy forever.
            if (event != ftxui::Event::Custom) {
                app_.screen_.PostEvent(ftxui::Event::Custom);
            }
            return true;
        }
    }
    if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Actions &&
        !app_.sidebar_has_focus_ && app_.actions_workspace_component_ &&
        app_.actions_workspace_component_->OnEvent(event)) {
        if (event != ftxui::Event::Custom) app_.screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }

    // Mouse input on the main screen is routed exclusively by FTXUI through
    // root_container_. Manual dispatch here competes with Container hit-testing
    // and can consume a menu click before it reaches MenuBarComponent.
    if (event.is_mouse()) {
        return false;
    }

    if (IsAltRightShortcut(event)) {
        if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Notes) {
            return false;
        }
        app_.FocusNextEditorPane();
        return true;
    }

    if (IsAltLeftShortcut(event)) {
        if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Notes) {
            return false;
        }
        app_.FocusPreviousEditorPane();
        return true;
    }

    if (HandleSidebarEvent(event)) {
        return true;
    }

    if (app_.RunMenuShortcut(event)) {
        return true;
    }

    if (HandleEditorEvent(event)) {
        return true;
    }

    return HandleFunctionKeyEvent(event);
}

bool AppEventDispatcher::HandleSidebarEvent(const ftxui::Event& event) {
    const bool sidebar_visible =
        app_.workspace_mode_ == TextltApp::WorkspaceMode::Actions
            ? app_.editor_config_.show_actions_sidebar
            : app_.workspace_mode_ == TextltApp::WorkspaceMode::Documents
            ? app_.editor_config_.show_file_explorer
            : false;
    const bool sidebar_is_focused =
        app_.ActiveLayer() == TextltApp::UiLayer::Main &&
        app_.sidebar_has_focus_ &&
        sidebar_visible &&
        !app_.layout_controller_.IsDistractionModeActive();
    if (!sidebar_is_focused) {
        return false;
    }

    if (event == ftxui::Event::Escape) {
        if (app_.workspace_mode_ == TextltApp::WorkspaceMode::Actions) {
            app_.sidebar_has_focus_ = false;
            app_.actions_workspace_component_->TakeFocus();
            return true;
        }
        app_.FocusEditor();
        return true;
    }

    if (app_.sidebar_panel_->OnEvent(event)) {
        if (event != ftxui::Event::Custom) {
            app_.screen_.PostEvent(ftxui::Event::Custom);
        }
        return true;
    }

    return false;
}

bool AppEventDispatcher::HandleEditorEvent(const ftxui::Event& event) {
    if (app_.workspace_mode_ != TextltApp::WorkspaceMode::Documents) return false;
    const bool editor_is_focused = app_.ActiveLayer() == TextltApp::UiLayer::Main && !app_.sidebar_has_focus_;
    if (!editor_is_focused) {
        return false;
    }

    if (app_.RunTextShortcut(event)) {
        return true;
    }

    // Handle Enter key for new line insertion in the editor.
    if (event == ftxui::Event::Return) {
        auto editor_ptr = std::static_pointer_cast<EditorComponent>(app_.text_editor_);
        if (editor_ptr && editor_ptr->IsReadOnly()) {
            app_.active_action_ = "Document is read-only";
            app_.screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }
        editor_ptr->HandleAutoIndentReturn();
        app_.screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }

    // Pass all other editor-specific events (characters, navigation, etc.) to the editor.
    return app_.text_editor_->OnEvent(event);
}

bool AppEventDispatcher::HandleFunctionKeyEvent(const ftxui::Event& event) {
    if (event == ftxui::Event::F1) {
        return app_.RunCommand("app.help");
    }

    if (event == ftxui::Event::F2 || event == ftxui::Event::F3 || event == ftxui::Event::F4) {
        if (app_.layout_controller_.IsDistractionModeActive()) {
            app_.active_action_ = "Menu is hidden in Distraction Mode";
            app_.screen_.PostEvent(ftxui::Event::Custom);
            return true;
        }

        if (event == ftxui::Event::F2) {
            app_.menu_bar_->OpenDropdown(0);
        } else if (event == ftxui::Event::F3) {
            app_.menu_bar_->OpenDropdown(1);
        } else {
            app_.menu_bar_->OpenDropdown(2);
        }
        app_.SetActiveLayer(TextltApp::UiLayer::Main);
        return true;
    }

    return false;
}

} // namespace textlt
