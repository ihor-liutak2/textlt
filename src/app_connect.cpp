#include "app.hpp"

#include "connect_workspace.hpp"
#include "connect_content.hpp"

namespace textlt {

void TextltApp::UpdateWorkspaceMenus() {
    connect_body_index_ = workspace_mode_ == WorkspaceMode::Connect ? 1 : 0;
    if (!connect_body_index_ && connect_workspace_) connect_workspace_->CloseMessageMenu();
    if (!menu_bar_) return;
    menu_bar_->SetNotesMenuVisible(workspace_mode_ == WorkspaceMode::Notes);
    menu_bar_->SetActionsMenuVisible(workspace_mode_ == WorkspaceMode::Actions);
    menu_bar_->SetConnectMenuVisible(workspace_mode_ == WorkspaceMode::Connect);
}

void TextltApp::ShowConnectWorkspace() {
    CloseDropdown();
    if (workspace_mode_ == WorkspaceMode::Connect) {
        FocusEditor();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    std::string error;
    if (workspace_mode_ == WorkspaceMode::Notes && notes_workspace_component_) {
        std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->Save(error);
        if (!error.empty()) active_action_ = "Notes save failed: " + error;
    } else if (workspace_mode_ == WorkspaceMode::Actions && actions_workspace_component_) {
        std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->Save(error);
        if (!error.empty()) active_action_ = "Actions save failed: " + error;
    }
    if (layout_controller_.IsDistractionModeActive()) SetDistractionEnabled(false);
    if (find_panel_open_) CloseFindPanel();
    workspace_mode_ = WorkspaceMode::Connect;
    UpdateWorkspaceMenus();
    FocusEditor();
    if (error.empty()) active_action_ = "Connect";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShareNoteViaConnect() {
    const auto workspace = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
    CloseDropdown();
    const auto* note = workspace->SelectedNote();
    if (!note) {
        active_action_ = "Select a note to send";
    } else {
        try {
            auto [metadata, payload] = ConnectSnapshot(*note);
            ShowConnectWorkspace();
            connect_workspace_->Share(std::move(metadata), std::move(payload));
        } catch (const std::exception& error) {
            active_action_ = error.what();
        }
    }
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShareActionViaConnect() {
    const auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    CloseDropdown();
    const auto* action = workspace->SelectedItem();
    if (!action) {
        active_action_ = "Select an action to send";
    } else {
        try {
            auto [metadata, payload] = ConnectSnapshot(*action);
            ShowConnectWorkspace();
            connect_workspace_->Share(std::move(metadata), std::move(payload));
        } catch (const std::exception& error) {
            active_action_ = error.what();
        }
    }
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenConnectSettingsModal(ConnectSettingsTab tab) {
    if (workspace_mode_ != WorkspaceMode::Connect) return;
    CloseDropdown();
    SetActiveLayer(UiLayer::ConnectSettings);
    connect_settings_modal_.Open(tab);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseConnectSettingsModal() {
    connect_settings_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenTelegramSettingsModal() {
    if (workspace_mode_ != WorkspaceMode::Connect) return;
    CloseDropdown();
    SetActiveLayer(UiLayer::TelegramSettings);
    telegram_settings_modal_.Open();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseTelegramSettingsModal() {
    telegram_settings_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenTextLTConfigModal() {
    if (workspace_mode_ != WorkspaceMode::Connect) return;
    CloseDropdown();
    SetActiveLayer(UiLayer::TextLTConfig);
    textlt_config_modal_.Open();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseTextLTConfigModal() {
    textlt_config_modal_.Close();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

} // namespace textlt
