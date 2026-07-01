#pragma once

#include <string>
#include <vector>

#include "remote/remote_connection_config.hpp"
#include "remote/remote_entry.hpp"

namespace textlt {

class IRemoteProvider {
public:
    virtual ~IRemoteProvider() = default;

    virtual bool Connect(const RemoteConnectionConfig& config, std::string& error) = 0;
    virtual bool TestConnection(std::string& output, std::string& error) = 0;
    virtual bool List(const std::string& path, std::vector<RemoteEntry>& entries, std::string& error) = 0;
    virtual bool Download(const std::string& remote_path, const std::string& local_path, std::string& error) = 0;
    virtual bool Upload(const std::string& local_path, const std::string& remote_path, std::string& error) = 0;
    virtual bool DownloadDirectory(const std::string& remote_path, const std::string& local_path, std::string& error) = 0;
    virtual bool UploadDirectory(const std::string& local_path, const std::string& remote_path, std::string& error) = 0;
    virtual bool Rename(const std::string& old_path, const std::string& new_path, std::string& error) = 0;
    virtual bool RemoveFile(const std::string& path, std::string& error) = 0;
    virtual bool MakeDirectory(const std::string& path, std::string& error) = 0;
    virtual bool RemoveDirectory(const std::string& path, std::string& error) = 0;
};

} // namespace textlt
