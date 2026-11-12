#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

struct ProjectRecord {
    std::string id;
    std::string name;
    std::filesystem::path local_root;
    std::string remote_connection_id;
};

class ProjectStore {
public:
    explicit ProjectStore(std::filesystem::path path = {});

    bool Load(std::string& error);
    bool Save(std::string& error) const;
    const std::vector<ProjectRecord>& Projects() const { return projects_; }
    const std::string& ActiveProjectId() const { return active_project_id_; }
    const ProjectRecord* ActiveProject() const;
    const ProjectRecord* FindById(const std::string& id) const;
    ProjectRecord* FindById(const std::string& id);

    // Returns the existing project when the normalized root is already registered.
    ProjectRecord* Add(std::filesystem::path root, std::string name, std::string& error);
    bool Remove(const std::string& id, std::string& error);
    bool SetActive(const std::string& id, std::string& error);
    bool SetRemoteConnection(const std::string& id, std::string connection_id, std::string& error);
    const std::filesystem::path& Path() const { return path_; }
    bool IsUsable() const { return usable_; }
    const std::string& LoadError() const { return load_error_; }

private:
    static std::filesystem::path NormalizeRoot(const std::filesystem::path& root);
    static std::string MakeId();
    void NormalizeActive();

    std::filesystem::path path_;
    std::vector<ProjectRecord> projects_;
    std::string active_project_id_;
    bool usable_ = true;
    std::string load_error_;
};

} // namespace textlt
