#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "remote/remote_command_runner.hpp"
#include "remote/remote_provider.hpp"

namespace textlt {

class RemoteFtpsProvider : public IRemoteProvider {
public:
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

    static bool ParseDirectoryListing(
        const std::string& listing,
        const std::string& directory,
        std::vector<RemoteEntry>& entries);

private:
    std::string BuildUrl(const std::string& path, bool directory) const;
    std::string BuildBaseConfig(const std::string& path, bool directory) const;
    bool RunCurl(const std::string& config_text, std::string& output, std::string& error) const;
    bool RunQuote(const std::vector<std::string>& commands, const std::string& directory, std::string& error) const;
    bool DownloadDirectoryRecursive(
        const std::string& remote_path,
        const std::filesystem::path& local_path,
        std::string& error);
    bool UploadDirectoryRecursive(
        const std::filesystem::path& local_path,
        const std::string& remote_path,
        std::string& error);

    RemoteConnectionConfig config_;
    RemoteCommandRunner runner_;
};

} // namespace textlt
