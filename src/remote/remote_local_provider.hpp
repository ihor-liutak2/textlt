#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "file_manager.hpp"
#include "remote/remote_provider.hpp"

namespace textlt {

class RemoteLocalProvider : public IRemoteProvider {
public:
    explicit RemoteLocalProvider(FileManager* file_manager);

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
    std::filesystem::path Resolve(const std::string& path) const;

    FileManager* file_manager_ = nullptr;
    std::filesystem::path root_;
};

} // namespace textlt
