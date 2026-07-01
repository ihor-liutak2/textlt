#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "remote/remote_provider.hpp"

namespace textlt {

class RemoteMicrosoftDriveProvider : public IRemoteProvider {
public:
    RemoteMicrosoftDriveProvider() = default;

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

    struct DriveItem {
        std::string id;
        std::string name;
        std::uintmax_t size = 0;
        bool is_folder = false;
        bool is_file = false;
    };

    static std::string MicrosoftRemotePathFromDisplayPath(const std::string& path);
    static std::string MicrosoftDriveBasePathFromConfig(const RemoteConnectionConfig& config);
    static std::string MicrosoftEncodePathSegments(const std::string& path);

private:
    bool EnsureConnected(std::string& error) const;
    std::string DriveBaseUrl() const;
    std::string ItemUrlForPath(const std::string& remote_path) const;
    std::string ChildrenUrlForPath(const std::string& remote_path) const;
    std::string ContentUrlForPath(const std::string& remote_path) const;
    bool ResolvePath(const std::string& remote_path, DriveItem& item, std::string& error);
    bool CreateFolderIfNeeded(const std::string& remote_path, std::string& error);
    bool DeletePath(const std::string& path, std::string& error);

    RemoteConnectionConfig config_;
    std::string access_token_;
    std::string token_type_ = "Bearer";
};

} // namespace textlt
