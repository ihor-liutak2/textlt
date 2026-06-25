#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace textlt {

class GitManager {
public:
    struct CommandResult {
        int exit_code = -1;
        std::string output;

        bool success() const { return exit_code == 0; }
    };

    struct StatusEntry {
        std::string path;
        char index_status = ' ';
        char worktree_status = ' ';

        bool IsStaged() const {
            return index_status != ' ' && index_status != '?';
        }
    };

    void SetWorkingDirectory(const std::filesystem::path& directory);
    void Invalidate();
    void RefreshNow();

    std::string GetCurrentBranch();
    std::map<std::string, char> GetFileStatuses();
    std::vector<StatusEntry> GetStatusEntries();
    std::filesystem::path RepositoryRoot();

    CommandResult StageFile(const std::string& path);
    CommandResult UnstageFile(const std::string& path);
    CommandResult DiffFile(const std::string& path, bool staged);
    CommandResult Commit(const std::string& message);

    std::vector<std::string> GetLocalBranches(std::string* current_branch = nullptr);
    CommandResult CheckoutBranch(const std::string& branch);
    CommandResult MergeBranch(const std::string& branch);
    CommandResult RenameBranch(const std::string& old_name, const std::string& new_name);
    CommandResult PullFastForward();

    CommandResult CheckOriginConnection();
    CommandResult FetchAllPrune();
    CommandResult Push();

private:
    struct Snapshot {
        std::filesystem::path working_directory;
        std::string branch;
        std::map<std::string, char> statuses;
        std::vector<StatusEntry> status_entries;
        std::filesystem::path repository_root;
    };

    static std::string ExecuteCommand(const std::string& command);
    static CommandResult ExecuteCommandWithStatus(const std::string& command);
    static Snapshot CollectSnapshot(std::filesystem::path working_directory);
    static std::string ShellQuote(const std::string& value);
    static std::string Trim(std::string value);
    static std::map<std::string, char> ParseStatusOutput(const std::string& output);
    static std::vector<StatusEntry> ParseStatusEntries(const std::string& output);

    CommandResult RunGitCommand(const std::vector<std::string>& args);
    std::string GitCommandPrefix();

    void RefreshIfNeeded();
    void ConsumeFinishedRefresh();

    std::filesystem::path working_directory_ = std::filesystem::current_path();
    std::string cached_branch_;
    std::map<std::string, char> cached_statuses_;
    std::vector<StatusEntry> cached_status_entries_;
    std::filesystem::path cached_repository_root_;
    std::chrono::steady_clock::time_point last_refresh_{};
    std::future<Snapshot> pending_refresh_;
};

} // namespace textlt
