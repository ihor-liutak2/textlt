#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "remote/remote_provider.hpp"

namespace textlt {

class RemoteGoogleDriveProvider : public IRemoteProvider {
public:
    RemoteGoogleDriveProvider() = default;

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
        std::string mime_type;
        std::uintmax_t size = 0;
        std::string parent_id;
    };

    static std::string GoogleRemotePathFromDisplayPath(const std::string& path);
    static std::string GoogleRootIdFromConfig(const RemoteConnectionConfig& config);
    static bool IsGoogleWorkspaceMimeType(const std::string& mime_type);
    static bool IsGoogleDocsMimeType(const std::string& mime_type);

private:
    bool EnsureConnected(std::string& error) const;
    bool ResolveDirectoryId(const std::string& remote_path, std::string& folder_id, std::string& error);
    bool ResolvePath(const std::string& remote_path, DriveItem& item, std::string& error);
    bool FindChildByName(
        const std::string& parent_id,
        const std::string& name,
        bool directory_only,
        DriveItem& item,
        std::string& error);
    bool CreateFolderIfNeeded(const std::string& remote_path, std::string& folder_id, std::string& error);
    bool DeletePath(const std::string& path, std::string& error);

    RemoteConnectionConfig config_;
    std::string access_token_;
    std::string token_type_ = "Bearer";
};

} // namespace textlt
