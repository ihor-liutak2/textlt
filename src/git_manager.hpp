#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <string>

namespace textlt {

class GitManager {
public:
    void SetWorkingDirectory(const std::filesystem::path& directory);
    void Invalidate();

    std::string GetCurrentBranch();
    std::map<std::string, char> GetFileStatuses();
    std::filesystem::path RepositoryRoot();

private:
    struct Snapshot {
        std::filesystem::path working_directory;
        std::string branch;
        std::map<std::string, char> statuses;
        std::filesystem::path repository_root;
    };

    static std::string ExecuteCommand(const std::string& command);
    static Snapshot CollectSnapshot(std::filesystem::path working_directory);
    static std::string ShellQuote(const std::string& value);
    static std::string Trim(std::string value);
    static std::map<std::string, char> ParseStatusOutput(const std::string& output);

    void RefreshIfNeeded();
    void ConsumeFinishedRefresh();

    std::filesystem::path working_directory_ = std::filesystem::current_path();
    std::string cached_branch_;
    std::map<std::string, char> cached_statuses_;
    std::filesystem::path cached_repository_root_;
    std::chrono::steady_clock::time_point last_refresh_{};
    std::future<Snapshot> pending_refresh_;
};

} // namespace textlt
