#include "app.hpp"

#include "path_utils.hpp"

#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

namespace textlt {

void TextltApp::InitializeProjects() {
    std::string error;
    if (!project_store_.IsUsable()) {
        active_action_ = project_store_.LoadError();
        return;
    }
    std::error_code filesystem_error;
    if (project_store_.Projects().empty()) {
        // An existing empty registry means the last project was removed.
        if (std::filesystem::exists(project_store_.Path(), filesystem_error)) return;
        if (filesystem_error) {
            active_action_ = filesystem_error.message();
            return;
        }
        if (!RegisterProject(std::filesystem::current_path(), error)) {
            active_action_ = error;
            return;
        }
    }

    const ProjectRecord* active = project_store_.ActiveProject();
    if (!active || !std::filesystem::is_directory(active->local_root, filesystem_error)) {
        RegisterProject(std::filesystem::current_path(), error);
        active = project_store_.ActiveProject();
    }
    if (!active) {
        active_action_ = error.empty() ? "Could not initialize projects" : error;
        return;
    }

    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->SetProjectRoot(active->local_root);
    git_manager_.SetWorkingDirectory(active->local_root);
    workspace_project_id_ = active->id;
}

void TextltApp::SyncNotesProjectContext() {
    if (!notes_workspace_component_) return;
    auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
    std::optional<notes::ProjectContext> active_project;
    if (const ProjectRecord* project = ActiveWorkspaceProject()) {
        active_project = notes::ProjectContext{project->id, project->name};
    }
    std::vector<std::string> known_project_ids;
    known_project_ids.reserve(project_store_.Projects().size());
    for (const ProjectRecord& project : project_store_.Projects()) {
        known_project_ids.push_back(project.id);
    }
    notes->SetProjectContext(
        std::move(active_project), std::move(known_project_ids), project_store_.IsUsable());
}

std::vector<SidebarPanel::ProjectEntry> TextltApp::SidebarProjects() const {
    std::vector<SidebarPanel::ProjectEntry> entries;
    entries.reserve(project_store_.Projects().size());
    for (const ProjectRecord& project : project_store_.Projects()) {
        std::error_code error;
        SidebarPanel::ProjectEntry entry;
        entry.id = project.id;
        entry.name = project.name;
        entry.root = project.local_root;
        entry.active = project.id == workspace_project_id_;
        entry.missing = !std::filesystem::is_directory(project.local_root, error);
        entry.has_remote = !project.remote_connection_id.empty();
        entry.remote_missing = entry.has_remote &&
            remote_config_store_.FindById(project.remote_connection_id) == nullptr;
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool TextltApp::RegisterProject(const std::filesystem::path& root, std::string& error) {
    return project_store_.Add(root, {}, error) != nullptr;
}

bool TextltApp::ActivateProject(const std::string& id) {
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project) {
        active_action_ = "Project is not registered";
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(project->local_root, filesystem_error)) {
        active_action_ = "Project folder is missing: " + Utf8FromPath(project->local_root);
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }

    std::string error;
    if (!project_store_.SetActive(id, error)) {
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }
    project = project_store_.FindById(id);
    workspace_project_id_ = project->id;
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->SetProjectRoot(project->local_root);
    git_manager_.SetWorkingDirectory(project->local_root);
    git_manager_.Invalidate();
    SyncNotesProjectContext();
    SyncActionsProjectContext();
    active_action_ = "Switched project: " + project->name;
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}

void TextltApp::OpenRenameProject(const std::string& id) {
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project) return;

    rename_project_id_ = project->id;
    project_name_ = project->name;
    project_name_cursor_position_ = static_cast<int>(project_name_.size());
    rename_project_error_.clear();
    CloseDropdown();
    SetActiveLayer(UiLayer::RenameProject);
    project_name_input_->TakeFocus();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SubmitRenameProject() {
    if (!project_store_.Rename(rename_project_id_, project_name_, rename_project_error_)) {
        project_name_input_->TakeFocus();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    const ProjectRecord& project = *project_store_.FindById(rename_project_id_);
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    const bool groups_updated = UpdateProjectGroupNames(project, rename_project_error_);
    RefreshActionsSidebar();
    if (!groups_updated) {
        rename_project_error_ = "Project name saved. " + rename_project_error_;
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    active_action_ = "Renamed project: " + project.name;
    CloseRenameProject();
}

void TextltApp::CloseRenameProject() {
    rename_project_id_.clear();
    FocusSidebar();
    screen_.PostEvent(ftxui::Event::Custom);
}

bool TextltApp::UpdateProjectGroupNames(const ProjectRecord& project, std::string& error) {
    auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
    auto actions = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    std::string notes_error;
    std::string actions_error;
    const bool notes_updated = notes->UpdateProjectGroupName({project.id, project.name}, notes_error);
    const bool actions_updated = actions->UpdateProjectGroupName(
        project.id, project.name, actions_error);
    error.clear();
    if (!notes_updated) error = "Notes group rename failed: " + notes_error;
    if (!actions_updated) {
        if (!error.empty()) error += "\n";
        error += "Actions group rename failed: " + actions_error;
    }
    return notes_updated && actions_updated;
}

void TextltApp::RemoveProject(const std::string& id) {
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project) {
        return;
    }
    const std::string name = project->name;
    std::string error;
    if (!project_store_.Remove(id, error)) {
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    const bool groups_detached = FinishProjectGroupDetachments(error);
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    const ProjectRecord* active = project_store_.ActiveProject();
    if (active) {
        sidebar->SetProjectRoot(active->local_root);
        git_manager_.SetWorkingDirectory(active->local_root);
        workspace_project_id_ = active->id;
    } else {
        workspace_project_id_.clear();
        sidebar->Refresh();
    }
    sidebar->ShowProjects();
    SyncNotesProjectContext();
    SyncActionsProjectContext();
    active_action_ = "Removed project registration: " + name;
    if (!groups_detached) active_action_ += ". " + error;
    screen_.PostEvent(ftxui::Event::Custom);
}

bool TextltApp::FinishProjectGroupDetachments(std::string& error) {
    auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
    auto actions = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
    const auto pending = project_store_.PendingGroupDetachments();
    error.clear();
    for (const std::string& id : pending) {
        std::string notes_error;
        std::string actions_error;
        const bool notes_detached = notes->DetachProjectGroup(id, notes_error);
        const bool actions_detached = actions->DetachProjectGroup(id, actions_error);
        if (!notes_detached) {
            error = "Could not detach Notes group: " + notes_error;
        } else if (!actions_detached) {
            error = "Could not detach Actions group: " + actions_error;
        } else {
            std::string save_error;
            if (!project_store_.FinishGroupDetachment(id, save_error)) {
                error = std::move(save_error);
            }
        }
    }
    return error.empty();
}

void TextltApp::OpenProjectRemote(std::string id) {
    // Activation rebuilds the sidebar, so keep our own copy of its project ID.
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project || (workspace_project_id_ != id && !ActivateProject(id))) return;

    std::string error;
    remote_config_store_.Load(error);
    const std::string& connection_id = project->remote_connection_id;
    if (connection_id.empty() || !remote_config_store_.FindById(connection_id)) {
        AssignProjectConnection(id);
        return;
    }

    if (menu_bar_) {
        menu_bar_->CloseDropdown();
    }
    remote_files_modal_.OpenForConnection(connection_id, false);
    active_action_ = "Opened project remote files: " + project->name;
    SetActiveLayer(UiLayer::RemoteFiles);
    screen_.PostEvent(ftxui::Event::Custom);
}

bool TextltApp::OpenWorkspaceFolder(
    const std::filesystem::path& root,
    std::string& error) {
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(root, filesystem_error)) {
        error = "Folder does not exist or is not a directory.";
        return false;
    }
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->SetProjectRoot(root);
    sidebar->ShowProject();
    git_manager_.SetWorkingDirectory(root);
    git_manager_.Invalidate();
    workspace_project_id_.clear();
    SyncNotesProjectContext();
    SyncActionsProjectContext();
    active_action_ = "Opened folder: " + Utf8FromPath(root);
    error.clear();
    return true;
}

const ProjectRecord* TextltApp::ActiveWorkspaceProject() const {
    return workspace_project_id_.empty()
        ? nullptr
        : project_store_.FindById(workspace_project_id_);
}

void TextltApp::AddCurrentFolderAsProject() {
    std::string error;
    if (!RegisterProject(CurrentSidebarDirectory(), error)) {
        active_action_ = error.empty() ? "Could not add current folder as a project" : error;
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    const std::string id = project_store_.ActiveProjectId();
    ActivateProject(id);
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->ShowProjects();
}

void TextltApp::AssignProjectConnection(const std::string& id) {
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project) {
        active_action_ = "Project is not registered";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    CloseDropdown();
    remote_connections_modal_.OpenForProject(
        project->id,
        project->name,
        project->remote_connection_id);
    active_action_ = "Choose a connection for project: " + project->name;
    SetActiveLayer(UiLayer::RemoteConnections);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::EditProjectConnection(const std::string& id) {
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project || project->remote_connection_id.empty()) {
        AssignProjectConnection(id);
        return;
    }
    std::string error;
    remote_config_store_.Load(error);
    if (!remote_config_store_.FindById(project->remote_connection_id)) {
        AssignProjectConnection(id);
        return;
    }
    remote_connections_modal_.OpenForEdit(project->remote_connection_id);
    active_action_ = "Editing project connection: " + project->name;
    SetActiveLayer(UiLayer::RemoteConnections);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::UnlinkProjectConnection(const std::string& id) {
    std::string error;
    if (!project_store_.SetRemoteConnection(id, {}, error)) {
        active_action_ = error.empty() ? "Could not unlink project connection" : error;
    } else {
        active_action_ = "Project remote connection unlinked";
        std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    }
    screen_.PostEvent(ftxui::Event::Custom);
}

bool TextltApp::SetProjectConnection(
    const std::string& project_id,
    const std::string& connection_id,
    std::string& error) {
    if (!remote_config_store_.FindById(connection_id)) {
        error = "Selected remote connection no longer exists.";
        return false;
    }
    if (!project_store_.SetRemoteConnection(project_id, connection_id, error)) {
        return false;
    }
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    active_action_ = "Project connection assigned";
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}

bool TextltApp::CanDeleteRemoteConnection(
    const std::string& connection_id,
    std::string& error) const {
    std::vector<std::string> projects;
    for (const ProjectRecord& project : project_store_.Projects()) {
        if (project.remote_connection_id == connection_id) {
            projects.push_back(project.name);
        }
    }
    if (projects.empty()) {
        return true;
    }
    error = "Connection is used by project" + std::string(projects.size() == 1 ? ": " : "s: ");
    for (size_t index = 0; index < projects.size(); ++index) {
        if (index > 0) {
            error += ", ";
        }
        error += projects[index];
    }
    error += ". Unlink it before deleting.";
    return false;
}

} // namespace textlt
