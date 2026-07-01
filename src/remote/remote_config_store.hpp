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

    RemoteConnectionConfig* FindById(const std::string& id);
    const RemoteConnectionConfig* FindById(const std::string& id) const;

    void AddOrUpdate(RemoteConnectionConfig config);
    bool RemoveById(const std::string& id);

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
    std::vector<RemoteConnectionConfig> connections_;
};

} // namespace textlt
