#pragma once

#include <string>
#include <vector>

#include "remote/remote_command_runner.hpp"
#include "remote/remote_provider.hpp"

namespace textlt {

class RemoteSftpProvider : public IRemoteProvider {
public:
    RemoteSftpProvider() = default;

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

private:
    std::vector<std::string> BuildSshArgs() const;
    std::vector<std::string> BuildSftpArgs() const;
    std::string Target() const;
    bool RunSftpBatch(const std::string& batch, std::string& output, std::string& error) const;
    static std::string SftpQuote(const std::string& value);

    RemoteConnectionConfig config_;
    RemoteCommandRunner runner_;
};

} // namespace textlt
