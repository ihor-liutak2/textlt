#include "project_store.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "json_utils.hpp"
#include "path_utils.hpp"
#include "windows_environment_path.hpp"

namespace textlt {
namespace {

std::filesystem::path DefaultProjectStorePath() {
#ifdef _WIN32
    const std::filesystem::path app_data = WindowsEnvironmentPath(L"APPDATA");
    if (!app_data.empty()) {
        return app_data / "textlt" / "projects.json";
    }
    const std::filesystem::path user_profile =
        WindowsEnvironmentPath(L"USERPROFILE");
    if (!user_profile.empty()) {
        return user_profile / "AppData" / "Roaming" / "textlt" /
            "projects.json";
    }
#else
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".config" / "textlt" / "projects.json";
    }
#endif
    return "projects.json";
}

std::string PathKey(const std::filesystem::path& path) {
    std::string key = Utf8FromPath(path.lexically_normal());
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
#endif
    return key;
}

} // namespace

ProjectStore::ProjectStore(std::filesystem::path path)
    : path_(path.empty() ? DefaultProjectStorePath() : std::move(path)) {
    std::string ignored;
    Load(ignored);
}

bool ProjectStore::Load(std::string& error) {
    usable_ = true;
    load_error_.clear();
    projects_.clear();
    pending_group_detachments_.clear();
    active_project_id_.clear();

    std::error_code filesystem_error;
    if (!std::filesystem::exists(path_, filesystem_error)) {
        error.clear();
        return true;
    }

    const Json root = LoadJsonValue(path_);
    if (!root.is_object()) {
        error = "Cannot read project registry: " + Utf8FromPath(path_);
        usable_ = false;
        load_error_ = error;
        return false;
    }

    active_project_id_ = JsonString(root, "active_project_id");
    const auto entries = root.find("projects");
    if (entries != root.end() && entries->is_array()) {
        for (const Json& item : *entries) {
            if (!item.is_object()) {
                continue;
            }
            ProjectRecord project;
            project.id = JsonString(item, "id");
            project.name = JsonString(item, "name");
            project.local_root = NormalizeRoot(
                textlt::PathFromUtf8(JsonString(item, "local_root")));
            project.remote_connection_id = JsonString(item, "remote_connection_id");
            if (project.id.empty() || project.local_root.empty()) {
                continue;
            }
            if (project.name.empty()) {
                project.name = Utf8FromPath(project.local_root.filename());
            }
            const std::string key = PathKey(project.local_root);
            const bool duplicate = std::any_of(projects_.begin(), projects_.end(), [&](const auto& existing) {
                return PathKey(existing.local_root) == key;
            });
            if (!duplicate) {
                projects_.push_back(std::move(project));
            }
        }
    }
    const auto detachments = root.find("pending_group_detachments");
    if (detachments != root.end() && detachments->is_array()) {
        for (const Json& item : *detachments) {
            if (!item.is_string()) continue;
            auto id = item.get<std::string>();
            if (!id.empty() && !FindById(id)) {
                pending_group_detachments_.push_back(std::move(id));
            }
        }
    }
    NormalizeActive();
    error.clear();
    return true;
}

bool ProjectStore::Save(std::string& error) const {
    if (!usable_) {
        error = load_error_.empty() ? "Project registry is not usable." : load_error_;
        return false;
    }
    Json root = Json::object();
    root["version"] = 1;
    root["active_project_id"] = active_project_id_;
    if (!pending_group_detachments_.empty()) {
        root["pending_group_detachments"] = pending_group_detachments_;
    }
    root["projects"] = Json::array();
    for (const ProjectRecord& project : projects_) {
        root["projects"].push_back({
            {"id", project.id},
            {"name", project.name},
            {"local_root", Utf8FromPath(project.local_root)},
            {"remote_connection_id", project.remote_connection_id},
        });
    }
    if (!WriteJsonAtomically(path_, root)) {
        error = "Cannot save project registry: " + Utf8FromPath(path_);
        return false;
    }
    error.clear();
    return true;
}

const ProjectRecord* ProjectStore::ActiveProject() const {
    return FindById(active_project_id_);
}

const ProjectRecord* ProjectStore::FindById(const std::string& id) const {
    const auto iter = std::find_if(projects_.begin(), projects_.end(), [&](const auto& project) {
        return project.id == id;
    });
    return iter == projects_.end() ? nullptr : &*iter;
}

ProjectRecord* ProjectStore::FindById(const std::string& id) {
    return const_cast<ProjectRecord*>(std::as_const(*this).FindById(id));
}

ProjectRecord* ProjectStore::Add(
    std::filesystem::path root,
    std::string name,
    std::string& error) {
    if (!usable_) {
        error = load_error_.empty() ? "Project registry is not usable." : load_error_;
        return nullptr;
    }
    root = NormalizeRoot(root);
    std::error_code filesystem_error;
    if (root.empty() || !std::filesystem::is_directory(root, filesystem_error)) {
        error = "Project folder does not exist or is not a directory.";
        return nullptr;
    }

    const std::string key = PathKey(root);
    for (ProjectRecord& project : projects_) {
        if (PathKey(project.local_root) == key) {
            const std::string previous_active = active_project_id_;
            active_project_id_ = project.id;
            if (!Save(error)) {
                active_project_id_ = previous_active;
                return nullptr;
            }
            return &project;
        }
    }

    ProjectRecord project;
    project.id = MakeId();
    project.local_root = std::move(root);
    project.name = std::move(name);
    if (project.name.empty()) {
        project.name = Utf8FromPath(project.local_root.filename());
    }
    if (project.name.empty()) {
        project.name = Utf8FromPath(project.local_root);
    }
    projects_.push_back(std::move(project));
    active_project_id_ = projects_.back().id;
    if (!Save(error)) {
        projects_.pop_back();
        NormalizeActive();
        return nullptr;
    }
    return &projects_.back();
}

bool ProjectStore::Rename(const std::string& id, std::string name, std::string& error) {
    if (!usable_) {
        error = load_error_;
        return false;
    }
    ProjectRecord* project = FindById(id);
    if (!project) {
        error = "Project is not registered.";
        return false;
    }
    const auto first = name.find_first_not_of(" \t\r\n\v\f");
    if (first == std::string::npos) {
        error = "Enter a project name.";
        return false;
    }
    name = name.substr(first, name.find_last_not_of(" \t\r\n\v\f") - first + 1);
    if (name.find_first_of("\r\n") != std::string::npos) {
        error = "Enter the project name on one line.";
        return false;
    }
    error.clear();
    if (project->name == name) return true;
    std::swap(project->name, name);
    if (Save(error)) return true;
    std::swap(project->name, name);
    return false;
}

bool ProjectStore::Remove(const std::string& id, std::string& error) {
    if (!usable_) {
        error = load_error_.empty() ? "Project registry is not usable." : load_error_;
        return false;
    }
    const auto iter = std::find_if(projects_.begin(), projects_.end(), [&](const auto& project) {
        return project.id == id;
    });
    if (iter == projects_.end()) {
        error = "Project is not registered.";
        return false;
    }
    const std::vector<ProjectRecord> previous_projects = projects_;
    const std::string previous_active = active_project_id_;
    // Save the cleanup request with the removal so either group write can be retried.
    pending_group_detachments_.push_back(iter->id);
    projects_.erase(iter);
    NormalizeActive();
    if (!Save(error)) {
        projects_ = previous_projects;
        active_project_id_ = previous_active;
        pending_group_detachments_.pop_back();
        return false;
    }
    return true;
}

bool ProjectStore::FinishGroupDetachment(std::string id, std::string& error) {
    auto previous = pending_group_detachments_;
    pending_group_detachments_.erase(std::remove(
        pending_group_detachments_.begin(), pending_group_detachments_.end(), id),
        pending_group_detachments_.end());
    error.clear();
    if (pending_group_detachments_.size() == previous.size() || Save(error)) return true;
    pending_group_detachments_ = std::move(previous);
    return false;
}

bool ProjectStore::SetActive(const std::string& id, std::string& error) {
    if (!usable_) {
        error = load_error_.empty() ? "Project registry is not usable." : load_error_;
        return false;
    }
    if (!FindById(id)) {
        error = "Project is not registered.";
        return false;
    }
    const std::string previous_active = active_project_id_;
    active_project_id_ = id;
    if (!Save(error)) {
        active_project_id_ = previous_active;
        return false;
    }
    return true;
}

bool ProjectStore::SetRemoteConnection(
    const std::string& id,
    std::string connection_id,
    std::string& error) {
    if (!usable_) {
        error = load_error_.empty() ? "Project registry is not usable." : load_error_;
        return false;
    }
    ProjectRecord* project = FindById(id);
    if (!project) {
        error = "Project is not registered.";
        return false;
    }
    const std::string previous_connection_id = project->remote_connection_id;
    project->remote_connection_id = std::move(connection_id);
    if (!Save(error)) {
        project->remote_connection_id = previous_connection_id;
        return false;
    }
    return true;
}

std::filesystem::path ProjectStore::NormalizeRoot(const std::filesystem::path& root) {
    if (root.empty()) {
        return {};
    }
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(root, error);
    if (error) {
        return {};
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(normalized, error);
    return error ? normalized.lexically_normal() : canonical;
}

std::string ProjectStore::MakeId() {
    // Local-only identity. Note groups link to projects through this ID, so
    // cross-device notes sync must replace it with a stable, portable
    // identifier before it can rely on project ownership.
    static std::atomic<unsigned long long> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return "project-" + std::to_string(ticks) + "-" + std::to_string(++sequence);
}

void ProjectStore::NormalizeActive() {
    if (projects_.empty()) {
        active_project_id_.clear();
    } else if (!FindById(active_project_id_)) {
        active_project_id_ = projects_.front().id;
    }
}

} // namespace textlt
