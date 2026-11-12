#include "app.hpp"

#include <filesystem>
#include <system_error>
#include <utility>

namespace textlt {

void TextltApp::InitializeProjects() {
    std::string error;
    if (!project_store_.IsUsable()) {
        active_action_ = project_store_.LoadError();
        return;
    }
    if (project_store_.Projects().empty()) {
        RegisterProject(std::filesystem::current_path(), error);
    }

    const ProjectRecord* active = project_store_.ActiveProject();
    std::error_code filesystem_error;
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
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(root, filesystem_error)) {
        error = "Project folder does not exist or is not a directory.";
        return false;
    }

    GitManager probe;
    probe.SetWorkingDirectory(root);
    probe.RefreshNow();
    const std::filesystem::path repository_root = probe.RepositoryRoot();
    const std::filesystem::path project_root = repository_root.empty() ? root : repository_root;
    return project_store_.Add(project_root, {}, error) != nullptr;
}

void TextltApp::ActivateProject(const std::string& id) {
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project) {
        active_action_ = "Project is not registered";
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(project->local_root, filesystem_error)) {
        active_action_ = "Project folder is missing: " + project->local_root.string();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    std::string error;
    if (!project_store_.SetActive(id, error)) {
        active_action_ = error;
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    project = project_store_.FindById(id);
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->SetProjectRoot(project->local_root);
    git_manager_.SetWorkingDirectory(project->local_root);
    git_manager_.Invalidate();
    workspace_project_id_ = project->id;
    sidebar->Refresh();
    active_action_ = "Switched project: " + project->name;
    screen_.PostEvent(ftxui::Event::Custom);
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
    if (project_store_.Projects().empty()) {
        RegisterProject(std::filesystem::current_path(), error);
    }
    const ProjectRecord* active = project_store_.ActiveProject();
    if (active) {
        auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
        sidebar->SetProjectRoot(active->local_root);
        sidebar->ShowProjects();
        git_manager_.SetWorkingDirectory(active->local_root);
        workspace_project_id_ = active->id;
    }
    active_action_ = "Removed project registration: " + name;
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::OpenProjectRemote(const std::string& id) {
    const ProjectRecord* project = project_store_.FindById(id);
    if (!project) {
        return;
    }
    ActivateProject(id);
    project = project_store_.FindById(id);
    if (!project || workspace_project_id_ != id) {
        return;
    }

    std::string error;
    remote_config_store_.Load(error);
    const std::string connection_id = project->remote_connection_id;
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
    active_action_ = "Opened folder: " + root.string();
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
