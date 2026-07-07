#include "app_event_dispatcher.hpp"

#include "app.hpp"

#include <memory>
#include <string>

#include "ftxui/component/mouse.hpp"
#include "keyboard_shortcuts.hpp"

namespace textlt {

namespace {

bool IsCtrlBShortcut(const ftxui::Event& event) {
    return MatchesShortcut(event, ShortcutModifier::Ctrl, 'b');
}

bool IsAltBShortcut(const ftxui::Event& event) {
    return MatchesShortcut(event, ShortcutModifier::Alt, 'b');
}

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

bool IsOpenedSidebarChordKey(const ftxui::Event& event) {
    const std::string& input = event.input();
    return input == "o" ||
        input == "O" ||
        input == "\x0F" ||
        input == "Ctrl+O" ||
        event == ftxui::Event::Special("Ctrl+O");
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

    if (app_.ActiveLayer() == TextltApp::UiLayer::Find) {
        return HandleFindPanelEvent(event);
    }

    return false;
}

bool AppEventDispatcher::HandleFindPanelEvent(const ftxui::Event& event) {
    const std::string& input = event.input();

    if (app_.find_input_->Focused() || app_.replace_find_input_->Focused()) {
        app_.active_search_panel_input_ = TextltApp::SearchPanelInput::Find;
    } else if (app_.replace_input_->Focused()) {
        app_.active_search_panel_input_ = TextltApp::SearchPanelInput::Replace;
    }

    if (event == ftxui::Event::Escape) {
        app_.CloseFindPanel();
        return true;
    }

    if (event == ftxui::Event::F3) {
        app_.FindNext();
        return true;
    }

    if (input == "\x1B[13;2u" || input == "\x1B[27;2;13~") {
        app_.FindPrevious();
        return true;
    }

    if (MatchesShortcut(event, ShortcutModifier::Ctrl, 'a')) {
        if (app_.find_input_->Focused()) {
            app_.find_input_cursor_position_ = static_cast<int>(app_.find_query_.size());
        } else if (app_.replace_find_input_->Focused()) {
            app_.replace_find_input_cursor_position_ = static_cast<int>(app_.find_query_.size());
        } else if (app_.replace_input_->Focused()) {
            app_.replace_input_cursor_position_ = static_cast<int>(app_.replace_text_.size());
        }
        return true;
    }

    return false;
}

bool AppEventDispatcher::HandleMainEvent(const ftxui::Event& event) {
    // Let FTXUI route menu events through the component tree. Calling
    // MenuBarComponent::OnEvent directly from this outer CatchEvent changes
    // layers in the middle of a mouse sequence when an item opens a modal.
    if (app_.menu_bar_->IsDropdownOpen()) {
        return event.is_mouse() ? false : app_.menu_bar_->OnEvent(event);
    }

    // Mouse input on the main screen is routed exclusively by FTXUI through
    // root_container_. Manual dispatch here competes with Container hit-testing
    // and can consume a menu click before it reaches MenuBarComponent.
    if (event.is_mouse()) {
        return false;
    }

    if (app_.pending_sidebar_chord_) {
        app_.pending_sidebar_chord_ = false;
        if (IsOpenedSidebarChordKey(event)) {
            return app_.RunCommand("sidebar.show_opened_files");
        }
    }

    if (IsAltBShortcut(event)) {
        app_.pending_sidebar_chord_ = false;
        return app_.RunCommand("sidebar.toggle_opened_project");
    }

    if (IsCtrlBShortcut(event)) {
        app_.pending_sidebar_chord_ = true;
        return app_.RunCommand("sidebar.ctrl_b_file_explorer");
    }

    if (IsAltRightShortcut(event)) {
        app_.FocusNextEditorPane();
        return true;
    }

    if (IsAltLeftShortcut(event)) {
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
    const bool sidebar_is_focused = app_.ActiveLayer() == TextltApp::UiLayer::Main && app_.sidebar_has_focus_;
    if (!sidebar_is_focused) {
        return false;
    }

    if (event == ftxui::Event::Escape) {
        app_.FocusEditor();
        return true;
    }

    if (app_.sidebar_panel_->OnEvent(event)) {
        app_.screen_.PostEvent(ftxui::Event::Custom);
        return true;
    }

    return false;
}

bool AppEventDispatcher::HandleEditorEvent(const ftxui::Event& event) {
    const bool editor_is_focused = app_.ActiveLayer() == TextltApp::UiLayer::Main && !app_.sidebar_has_focus_;
    if (!editor_is_focused) {
        return false;
    }

    if (app_.HandleTerminalBracketedPaste(event)) {
        return true;
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

    if (event == ftxui::Event::F2) {
        app_.menu_bar_->OpenDropdown(0);
        app_.SetActiveLayer(TextltApp::UiLayer::Main);
        return true;
    }

    if (event == ftxui::Event::F3) {
        app_.menu_bar_->OpenDropdown(1);
        app_.SetActiveLayer(TextltApp::UiLayer::Main);
        return true;
    }

    if (event == ftxui::Event::F4) {
        app_.menu_bar_->OpenDropdown(2);
        app_.SetActiveLayer(TextltApp::UiLayer::Main);
        return true;
    }

    return false;
}

} // namespace textlt
