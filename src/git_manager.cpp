#include "git_manager.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <utility>

namespace textlt {
namespace {

constexpr auto kGitRefreshInterval = std::chrono::milliseconds(1200);

FILE* OpenPipe(const std::string& command, const char* mode) {
#ifdef _WIN32
    return _popen(command.c_str(), mode);
#else
    return popen(command.c_str(), mode);
#endif
}

int ClosePipe(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

std::string StderrToNull() {
#ifdef _WIN32
    return "2>nul";
#else
    return "2>/dev/null";
#endif
}

std::string StderrToStdout() {
#ifdef _WIN32
    return "2>&1";
#else
    return "2>&1";
#endif
}

} // namespace

void GitManager::SetWorkingDirectory(const std::filesystem::path& directory) {
    if (directory.empty() || directory == working_directory_) {
        return;
    }

    working_directory_ = directory;
    Invalidate();
}

void GitManager::Invalidate() {
    last_refresh_ = {};
}

void GitManager::RefreshNow() {
    if (pending_refresh_.valid()) {
        pending_refresh_.wait();
        ConsumeFinishedRefresh();
    }

    Snapshot snapshot = CollectSnapshot(working_directory_);
    cached_branch_ = std::move(snapshot.branch);
    cached_statuses_ = std::move(snapshot.statuses);
    cached_status_entries_ = std::move(snapshot.status_entries);
    cached_repository_root_ = std::move(snapshot.repository_root);
    last_refresh_ = std::chrono::steady_clock::now();
}

std::string GitManager::GetCurrentBranch() {
    RefreshIfNeeded();
    return cached_branch_;
}

std::map<std::string, char> GitManager::GetFileStatuses() {
    RefreshIfNeeded();
    return cached_statuses_;
}

std::vector<GitManager::StatusEntry> GitManager::GetStatusEntries() {
    RefreshIfNeeded();
    return cached_status_entries_;
}

std::filesystem::path GitManager::RepositoryRoot() {
    RefreshIfNeeded();
    return cached_repository_root_;
}

GitManager::CommandResult GitManager::StageFile(const std::string& path) {
    return RunGitCommand({"add", "--", path});
}

GitManager::CommandResult GitManager::UnstageFile(const std::string& path) {
    return RunGitCommand({"restore", "--staged", "--", path});
}

GitManager::CommandResult GitManager::DiffFile(const std::string& path, bool staged) {
    if (staged) {
        return RunGitCommand({"diff", "--cached", "--", path});
    }
    return RunGitCommand({"diff", "--", path});
}

GitManager::CommandResult GitManager::Commit(const std::string& message) {
    return RunGitCommand({"commit", "-m", message});
}

std::vector<std::string> GitManager::GetLocalBranches(std::string* current_branch) {
    std::vector<std::string> branches;
    CommandResult result = RunGitCommand({"branch", "--format=%(refname:short)"});
    if (!result.success()) {
        return branches;
    }

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (!line.empty()) {
            branches.push_back(line);
        }
    }

    std::string current = Trim(RunGitCommand({"branch", "--show-current"}).output);
    if (current_branch) {
        *current_branch = current;
    }

    if (!current.empty()) {
        auto position = std::find(branches.begin(), branches.end(), current);
        if (position != branches.end() && position != branches.begin()) {
            std::rotate(branches.begin(), position, position + 1);
        }
    }

    return branches;
}

GitManager::CommandResult GitManager::CheckoutBranch(const std::string& branch) {
    return RunGitCommand({"checkout", branch});
}

GitManager::CommandResult GitManager::MergeBranch(const std::string& branch) {
    return RunGitCommand({"merge", branch});
}

GitManager::CommandResult GitManager::RenameBranch(
    const std::string& old_name,
    const std::string& new_name) {
    const std::string current = Trim(RunGitCommand({"branch", "--show-current"}).output);
    if (!current.empty() && current == old_name) {
        return RunGitCommand({"branch", "-m", new_name});
    }
    return RunGitCommand({"branch", "-m", old_name, new_name});
}

GitManager::CommandResult GitManager::PullFastForward() {
    return RunGitCommand({"pull", "--ff-only"});
}

GitManager::CommandResult GitManager::CheckOriginConnection() {
    return RunGitCommand({"ls-remote", "--heads", "origin"});
}

GitManager::CommandResult GitManager::FetchAllPrune() {
    return RunGitCommand({"fetch", "--all", "--prune"});
}

GitManager::CommandResult GitManager::Push() {
    return RunGitCommand({"push"});
}

void GitManager::RefreshIfNeeded() {
    ConsumeFinishedRefresh();

    if (pending_refresh_.valid()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (last_refresh_.time_since_epoch().count() != 0 &&
        now - last_refresh_ < kGitRefreshInterval) {
        return;
    }

    const std::filesystem::path directory = working_directory_;
    pending_refresh_ = std::async(
        std::launch::async,
        [directory] { return CollectSnapshot(directory); });
    last_refresh_ = now;
}

void GitManager::ConsumeFinishedRefresh() {
    if (!pending_refresh_.valid()) {
        return;
    }

    if (pending_refresh_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return;
    }

    Snapshot snapshot = pending_refresh_.get();
    if (snapshot.working_directory != working_directory_) {
        last_refresh_ = {};
        return;
    }

    cached_branch_ = std::move(snapshot.branch);
    cached_statuses_ = std::move(snapshot.statuses);
    cached_status_entries_ = std::move(snapshot.status_entries);
    cached_repository_root_ = std::move(snapshot.repository_root);
}

std::string GitManager::ExecuteCommand(const std::string& command) {
    CommandResult result = ExecuteCommandWithStatus(command);
    return result.success() ? result.output : "";
}

GitManager::CommandResult GitManager::ExecuteCommandWithStatus(const std::string& command) {
    std::array<char, 256> buffer{};
    CommandResult result;

    FILE* pipe = OpenPipe(command, "r");
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

    result.exit_code = ClosePipe(pipe);
    return result;
}

GitManager::Snapshot GitManager::CollectSnapshot(std::filesystem::path working_directory) {
    Snapshot snapshot;
    snapshot.working_directory = working_directory;
    const std::string quoted_directory = ShellQuote(working_directory.string());
    const std::string git_prefix = "git -C " + quoted_directory + " ";

    snapshot.branch = Trim(ExecuteCommand(git_prefix + "branch --show-current " + StderrToNull()));
    snapshot.repository_root = Trim(ExecuteCommand(
        git_prefix + "rev-parse --show-toplevel " + StderrToNull()));

    const std::string status_output = ExecuteCommand(
        git_prefix + "status --porcelain " + StderrToNull());
    snapshot.statuses = ParseStatusOutput(status_output);
    snapshot.status_entries = ParseStatusEntries(status_output);

    if (snapshot.repository_root.empty()) {
        snapshot.branch.clear();
        snapshot.statuses.clear();
        snapshot.status_entries.clear();
    }

    return snapshot;
}

std::string GitManager::ShellQuote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char character : value) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted += character;
        }
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

std::string GitManager::Trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
            value.back() == '\t')) {
        value.pop_back();
    }

    size_t start = 0;
    while (start < value.size() &&
           (value[start] == '\n' || value[start] == '\r' || value[start] == ' ' ||
            value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::map<std::string, char> GitManager::ParseStatusOutput(const std::string& output) {
    std::map<std::string, char> statuses;
    for (const StatusEntry& entry : ParseStatusEntries(output)) {
        const char status = entry.index_status != ' ' && entry.index_status != '?'
            ? entry.index_status
            : entry.worktree_status;
        statuses[entry.path] = status;
    }
    return statuses;
}

std::vector<GitManager::StatusEntry> GitManager::ParseStatusEntries(const std::string& output) {
    std::vector<StatusEntry> entries;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.size() < 4) {
            continue;
        }

        StatusEntry entry;
        entry.index_status = line[0];
        entry.worktree_status = line[1];
        entry.path = line.substr(3);

        const std::string rename_separator = " -> ";
        const size_t rename_position = entry.path.find(rename_separator);
        if (rename_position != std::string::npos) {
            entry.path = entry.path.substr(rename_position + rename_separator.size());
        }

        if (!entry.path.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

GitManager::CommandResult GitManager::RunGitCommand(const std::vector<std::string>& args) {
    std::string command = GitCommandPrefix();
    for (const std::string& arg : args) {
        command += " " + ShellQuote(arg);
    }
    command += " " + StderrToStdout();

    CommandResult result = ExecuteCommandWithStatus(command);
    Invalidate();
    return result;
}

std::string GitManager::GitCommandPrefix() {
    const std::filesystem::path directory = cached_repository_root_.empty()
        ? working_directory_
        : cached_repository_root_;
    return "git -C " + ShellQuote(directory.string());
}

} // namespace textlt
