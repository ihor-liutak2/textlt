#pragma once

#include <string>
#include <vector>

#include "remote/remote_provider.hpp"

namespace textlt {

class RemoteDropboxProvider : public IRemoteProvider {
public:
    RemoteDropboxProvider() = default;

    bool Connect(const RemoteConnectionConfig& config, std::string& error) override;
    bool TestConnection(std::string& output, std::string& error) override;
    bool List(const std::string& path, std::vector<RemoteEntry>& entries, std::string& error) override;
    bool Download(const std::string& remote_path, const std::string& local_path, std::string& error) override;
    bool Upload(const std::string& local_path, const std::string& remote_path, std::string& error) override;
    bool DownloadDirectory(const std::string& remote_path, const std::string& local_path, std::string& error) override;
    bool UploadDirectory(const std::string& local_path, const std::string& remote_path, std::string& error) override;
    bool Rename(const std::string& old_path, const std::string& new_path, std::string& error) override;
    bool RemoveFile(const std::string& path, std::string& error) override;
    bool MakeDirectory(const std::string& path, std::string& error) override;
    bool RemoveDirectory(const std::string& path, std::string& error) override;

    static std::string DropboxApiPathFromRemotePath(const std::string& path);
    static std::string RemotePathFromDropboxDisplayPath(const std::string& path);

private:
    bool DeletePath(const std::string& path, std::string& error);
    bool MakeDirectoryIfNeeded(const std::string& path, std::string& error);
    bool EnsureConnected(std::string& error) const;

    RemoteConnectionConfig config_;
    std::string access_token_;
    std::string token_type_ = "Bearer";
};

} // namespace textlt
