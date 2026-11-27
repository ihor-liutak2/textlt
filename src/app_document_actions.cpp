#include "app.hpp"

#include "connect_workspace.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "editor/document_session.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {


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
    SetActiveLayer(UiLayer::Main);

    // Closing an application-level modal must return focus to the workspace
    // it was opened from. Taking focus on an editor child while Notes or
    // Actions is active makes Container::Tab select the Documents child even
    // though workspace_mode_ still renders the other workspace.
    if (workspace_mode_ == WorkspaceMode::Connect) {
        connect_workspace_->TakeFocus();
        return;
    }
    if (workspace_mode_ == WorkspaceMode::Notes) {
        workspace_mode_index_ = static_cast<int>(workspace_mode_);
        if (notes_workspace_component_) {
            notes_workspace_component_->TakeFocus();
        }
        return;
    }
    if (workspace_mode_ == WorkspaceMode::Actions) {
        workspace_mode_index_ = static_cast<int>(workspace_mode_);
        if (actions_workspace_component_) {
            actions_workspace_component_->TakeFocus();
        }
        return;
    }

    workspace_mode_index_ = static_cast<int>(WorkspaceMode::Documents);
    document_workspace_.ClampActiveEditorPaneIndex(editor_pane_components_.size());
    if (!editor_pane_components_.empty()) {
        text_editor_ = editor_pane_components_[document_workspace_.ActiveEditorPaneIndex()];
    }
    if (text_editor_) {
        text_editor_->TakeFocus();
    }
}


void TextltApp::FocusSidebar() {
    if (workspace_mode_ == WorkspaceMode::Connect) return;
    sidebar_has_focus_ = true;
    SetActiveLayer(UiLayer::Main);
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->FocusMenu();
}


void TextltApp::ToggleFileExplorer() {
    if (workspace_mode_ == WorkspaceMode::Connect) return;
    if (workspace_mode_ == WorkspaceMode::Notes && notes_workspace_component_) {
        const bool visible = !editor_config_.show_notes_sidebar;
        editor_config_.show_notes_sidebar = visible;

        auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(
            notes_workspace_component_);
        notes->SetSidebarVisible(visible);
        if (visible) {
            notes->FocusSidebar();
        } else {
            notes->FocusMainArea();
        }
        sidebar_has_focus_ = false;
        workspace_mode_index_ = static_cast<int>(workspace_mode_);
        notes->TakeFocus();
        SetActiveLayer(UiLayer::Main);
        active_action_ = visible ? "Notes sidebar enabled" : "Notes sidebar disabled";
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (workspace_mode_ == WorkspaceMode::Actions) {
        const bool visible = !editor_config_.show_actions_sidebar;
        editor_config_.show_actions_sidebar = visible;

        if (visible) {
            auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
            sidebar->ShowActions();
            FocusSidebar();
            active_action_ = "Actions sidebar enabled";
        } else {
            FocusEditor();
            active_action_ = "Actions sidebar disabled";
        }
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    editor_config_.show_file_explorer = !editor_config_.show_file_explorer;
    if (editor_config_.show_file_explorer) {
        active_action_ = "File Explorer enabled";
        FocusSidebar();
    } else {
        active_action_ = "File Explorer disabled";
        FocusEditor();
    }
    SaveConfig();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::ShowOpenFileList() {
    CloseDropdown();
    open_files_bar_.OpenFileList();
    active_action_ = "Opened files";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShowProjectSidebar() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        SaveConfig();
    }

    sidebar->ShowProject();
    active_action_ = "Folder files";
    FocusSidebar();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShowProjectsSidebar() {
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        SaveConfig();
    }
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ShowProjects();
    FocusSidebar();
    active_action_ = "Projects";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShowFavoritesSidebar() {
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        SaveConfig();
    }

    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ShowFavorites();
    FocusSidebar();
    active_action_ = "Favorite files";
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::ToggleOpenFilesAndFolder() {
    const auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    if (sidebar_has_focus_ && sidebar->IsProjectMode()) {
        ShowOpenFileList();
    } else {
        CloseDropdown();
        ShowProjectSidebar();
    }
}


void TextltApp::CopySidebarSelectedPath() {
    const auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    if (!sidebar) {
        active_action_ = "Project sidebar is unavailable";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    sidebar->CopySelectedProjectPath();
}


void TextltApp::RefreshProjectSidebar() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ShowProject();
    sidebar->Refresh();
}


std::shared_ptr<DocumentSession> TextltApp::ActiveSessionPtr() const {
    const auto document = document_workspace_.ActiveSessionPtr();
    if (document) {
        return document;
    }
    const auto editor = ActiveEditor();
    return editor ? editor->GetSession() : nullptr;
}


void TextltApp::AddOpenSession(std::shared_ptr<DocumentSession> doc) {
    if (!doc) {
        return;
    }

    document_file_controller_.AddSession(std::move(doc));
    BindEditorComponentsToWorkspace();
}


void TextltApp::EnsureOneOpenSession() {
    document_file_controller_.EnsureOneSession(VisibleEditorPaneCount());
    BindEditorComponentsToWorkspace();
}


void TextltApp::CloseCurrentFile() {
    RequestCloseOpenSession(document_workspace_.ActiveSessionIndex());
}


void TextltApp::CloseAllOpenedFiles() {
    CloseGitCompare();
    document_file_controller_.CloseAllSessions(VisibleEditorPaneCount());
    BindEditorComponentsToWorkspace();
    UpdateFileMenuLabels();
    active_action_ = "Closed all files";
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::RequestCloseOpenSession(size_t index) {
    const auto document = document_workspace_.SessionPtrAt(index);
    if (!document) return;

    if (document->is_dirty) {
        pending_close_session_ = document;
        ShowUnsavedChangesDialog(document->DisplayTitle());
    } else {
        CloseOpenSession(document);
    }
}

void TextltApp::CloseOpenSession(const std::shared_ptr<DocumentSession>& document) {
    const auto& sessions = document_workspace_.OpenSessions();
    const auto found = std::find(sessions.begin(), sessions.end(), document);
    if (found == sessions.end()) return;

    if (IsGitCompareSession(document)) {
        CloseGitCompare();
    } else {
        document_file_controller_.RemoveSession(
            static_cast<size_t>(found - sessions.begin()), VisibleEditorPaneCount());
        BindEditorComponentsToWorkspace();
        active_action_ = "Closed " + document->DisplayTitle();
    }
    UpdateFileMenuLabels();
    // During Save As, the file picker finishes processing the event before
    // the dispatcher restores focus to the editor.
    if (ActiveLayer() != UiLayer::Files) CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::PersistOpenedSessions() {
    document_file_controller_.PersistOpenedSessions();
}


void TextltApp::RestoreOpenedSessions() {
    if (document_file_controller_.RestoreOpenedSessions(VisibleEditorPaneCount())) {
        BindEditorComponentsToWorkspace();
    }
}


void TextltApp::ActivateOpenSession(size_t index) {
    if (!document_file_controller_.ActivateSession(index, VisibleEditorPaneCount())) {
        return;
    }

    if (!IsGitCompareSession(document_workspace_.ActiveSessionPtr())) CloseGitCompare();
    BindEditorComponentsToWorkspace();
    const auto editor_ptr = ActiveEditor();
    UpdateFileMenuLabels();
    active_action_ = "Switched to " + (editor_ptr ? editor_ptr->CurrentFilePath() : "file");
    screen_.PostEvent(ftxui::Event::Custom);
}


std::string TextltApp::ActiveSessionFavoritePath() const {
    return document_file_controller_.ActiveSessionFavoritePath();
}


void TextltApp::PersistActiveFavoriteCursor() {
    document_file_controller_.PersistActiveFavoriteCursor();
}


void TextltApp::RestoreFavoriteCursor(const std::string& path) {
    document_file_controller_.RestoreFavoriteCursor(path);
}


void TextltApp::ToggleActiveFavorite() {
    const auto result = document_file_controller_.ToggleActiveFavorite();
    if (result.status == DocumentFileController::FavoriteToggleStatus::NeedsSave) {
        active_action_ = "Save the file before adding it to favorites";
        NotifyWarning("Save the file before adding it to favorites.");
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (result.status == DocumentFileController::FavoriteToggleStatus::Removed) {
        active_action_ = "Removed favorite " + result.path;
        NotifySuccess("Removed from favorites.");
    } else {
        active_action_ = "Added favorite " + result.path;
        NotifySuccess("Added to favorites.");
    }

    UpdateFileMenuLabels();
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::AddActiveFavoriteFromSidebar() {
    const auto result = document_file_controller_.AddActiveFavorite();
    if (result.status == DocumentFileController::FavoriteToggleStatus::NeedsSave) {
        active_action_ = "Save the file before adding it to favorites";
        NotifyWarning("Save the file before adding it to favorites.");
    } else if (result.status == DocumentFileController::FavoriteToggleStatus::AlreadyFavorite) {
        active_action_ = "Already favorite " + result.path;
        NotifyWarning("Already in favorites.");
    } else {
        active_action_ = "Added favorite " + result.path;
        NotifySuccess("Added to favorites.");
    }

    UpdateFileMenuLabels();
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    screen_.PostEvent(ftxui::Event::Custom);
}


} // namespace textlt
