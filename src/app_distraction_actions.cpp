#include "app.hpp"

#include <memory>

#include "editor/document_session.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {

void TextltApp::OpenDistractionOptionsModal() {
    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    SetActiveLayer(UiLayer::DistractionOptions);
    distraction_options_modal_.Open();
    active_action_ = "Opened Distraction Mode options";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseDistractionOptionsModal() {
    distraction_options_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

DistractionTopBarState TextltApp::CurrentDistractionTopBarState() const {
    return distraction_controller_.TopBarState(
        &document_workspace_,
        layout_controller_.VisiblePaneCount(),
        &editor_config_);
}


void TextltApp::ApplyDistractionSettings(DistractionModeSettings settings) {
    distraction_controller_.SetSettings(settings);
    editor_config_.distraction_mode = distraction_controller_.Settings();
    SaveConfig();
    BindEditorComponentsToWorkspace();
    distraction_controller_.SyncPageColumns(
        &document_workspace_,
        layout_controller_.VisiblePaneCount(),
        &editor_config_);
    active_action_ = "Distraction Mode settings saved";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SetDistractionEnabled(bool enabled) {
    distraction_controller_.SetEnabled(enabled);
    editor_config_.distraction_mode = distraction_controller_.Settings();
    SaveConfig();
    if (enabled) {
        sidebar_has_focus_ = false;
    }
    BindEditorComponentsToWorkspace();
    distraction_controller_.SyncPageColumns(
        &document_workspace_,
        layout_controller_.VisiblePaneCount(),
        &editor_config_);
    active_action_ = enabled ? "Distraction Mode enabled" : "Distraction Mode disabled";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CommandDistractionOpenOptions() {
    OpenDistractionOptionsModal();
}

void TextltApp::CommandDistractionEnter() {
    SetDistractionEnabled(true);
    if (distraction_options_modal_.IsOpen()) {
        CloseDistractionOptionsModal();
    } else {
        FocusEditor();
    }
}

void TextltApp::CommandDistractionExit() {
    SetDistractionEnabled(false);
    if (distraction_options_modal_.IsOpen()) {
        CloseDistractionOptionsModal();
    } else {
        FocusEditor();
    }
}

void TextltApp::CommandDistractionToggle() {
    SetDistractionEnabled(!distraction_controller_.Enabled());
    FocusEditor();
}

void TextltApp::CommandDistractionNextPage() {
    if (distraction_controller_.NextPage(
            &document_workspace_,
            layout_controller_.VisiblePaneCount(),
            &editor_config_)) {
        active_action_ = "Distraction Mode: next page";
    }
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CommandDistractionPreviousPage() {
    if (distraction_controller_.PreviousPage(
            &document_workspace_,
            layout_controller_.VisiblePaneCount(),
            &editor_config_)) {
        active_action_ = "Distraction Mode: previous page";
    }
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CommandDistractionGoToPage() {
    OpenDistractionPagePanel();
}

} // namespace textlt
