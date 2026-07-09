#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "remote/remote_connection_config.hpp"

namespace textlt {

class RemoteConfigStore {
public:
    explicit RemoteConfigStore(std::filesystem::path path = {});

    bool Load(std::string& error);
    bool Save(std::string& error) const;

    const std::vector<RemoteConnectionConfig>& Connections() const { return connections_; }
    std::vector<RemoteConnectionConfig>& MutableConnections() { return connections_; }

    const std::string& ActiveConnectionId() const { return active_connection_id_; }
    void SetActiveConnectionId(std::string id);

    RemoteConnectionConfig* FindById(const std::string& id);
    const RemoteConnectionConfig* FindById(const std::string& id) const;
    RemoteConnectionConfig* FindActiveConnection();
    const RemoteConnectionConfig* FindActiveConnection() const;

    void AddOrUpdate(RemoteConnectionConfig config);
    bool RemoveById(const std::string& id);

    const std::filesystem::path& Path() const { return path_; }

private:
    void NormalizeActiveConnection();

    std::filesystem::path path_;
    std::vector<RemoteConnectionConfig> connections_;
    std::string active_connection_id_;
};

} // namespace textlt
