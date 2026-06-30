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

    struct RemoteEntry {
        std::string name;
        std::string fetch_url;
        std::string push_url;
    };

    struct CompareRef {
        std::string label;
        std::string value;
    };

    struct CompareEntry {
        std::string path;
        std::string old_path;
        std::string status;
    };

    struct GitIdentity {
        std::string effective_name;
        std::string effective_email;
        std::string local_name;
        std::string local_email;
        std::string global_name;
        std::string global_email;
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
    CommandResult RebaseBranch(const std::string& branch);
    CommandResult DeleteLocalBranch(const std::string& branch);
    CommandResult RenameBranch(const std::string& old_name, const std::string& new_name);
    CommandResult PullFastForward();

    std::vector<std::string> GetRemoteBranches();
    CommandResult CheckoutRemoteBranch(const std::string& remote_branch);
    CommandResult DeleteRemoteBranch(const std::string& remote_branch);

    std::vector<std::string> GetTags();
    CommandResult CreateTag(const std::string& tag_name, const std::string& message);
    CommandResult DeleteTag(const std::string& tag_name);
    CommandResult PushTag(const std::string& tag_name);
    CommandResult PushAllTags();
    CommandResult FetchTags();

    CommandResult CheckOriginConnection();
    CommandResult FetchAllPrune();
    CommandResult Push();
    CommandResult ForcePushWithLease();

    std::vector<RemoteEntry> GetRemotes();
    CommandResult AddRemote(const std::string& name, const std::string& url);
    CommandResult SetRemoteUrl(const std::string& name, const std::string& url);
    CommandResult RenameRemote(const std::string& old_name, const std::string& new_name);
    CommandResult RemoveRemote(const std::string& name);
    CommandResult TestRemote(const std::string& name);

    GitIdentity GetIdentity();
    CommandResult SaveLocalIdentity(const std::string& name, const std::string& email);
    CommandResult SaveGlobalIdentity(const std::string& name, const std::string& email);
    CommandResult ClearLocalIdentity();

    std::vector<std::string> GetConfigList(bool global_scope);

    std::vector<CompareRef> GetCompareRefs(size_t recent_commit_limit = 30);
    std::vector<CompareEntry> GetCompareEntries(
        const std::string& left_ref,
        const std::string& right_ref);
    CommandResult CompareDiff(
        const std::string& left_ref,
        const std::string& right_ref,
        const std::string& path);
    CommandResult ReadFileAtRef(const std::string& ref, const std::string& path);

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
    static std::vector<CompareEntry> ParseCompareEntries(const std::string& output);
    static bool IsWorkingTreeRef(const std::string& ref);

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
