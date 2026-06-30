#include "git_manager.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <set>
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

GitManager::CommandResult GitManager::RebaseBranch(const std::string& branch) {
    return RunGitCommand({"rebase", branch});
}

GitManager::CommandResult GitManager::DeleteLocalBranch(const std::string& branch) {
    return RunGitCommand({"branch", "-d", branch});
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

std::vector<std::string> GitManager::GetRemoteBranches() {
    std::vector<std::string> branches;
    CommandResult result = RunGitCommand({"branch", "-r", "--format=%(refname:short)"});
    if (!result.success()) {
        return branches;
    }

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line.find("HEAD") != std::string::npos) {
            continue;
        }
        branches.push_back(line);
    }
    return branches;
}

GitManager::CommandResult GitManager::CheckoutRemoteBranch(const std::string& remote_branch) {
    return RunGitCommand({"checkout", "--track", remote_branch});
}

GitManager::CommandResult GitManager::DeleteRemoteBranch(const std::string& remote_branch) {
    const size_t slash = remote_branch.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= remote_branch.size()) {
        CommandResult result;
        result.exit_code = 1;
        result.output = "Remote branch must have the form remote/name.";
        return result;
    }
    const std::string remote = remote_branch.substr(0, slash);
    const std::string branch = remote_branch.substr(slash + 1);
    return RunGitCommand({"push", remote, "--delete", branch});
}

std::vector<std::string> GitManager::GetTags() {
    std::vector<std::string> tags;
    CommandResult result = RunGitCommand({"tag", "--list"});
    if (!result.success()) {
        return tags;
    }

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (!line.empty()) {
            tags.push_back(line);
        }
    }
    return tags;
}

GitManager::CommandResult GitManager::CreateTag(
    const std::string& tag_name,
    const std::string& message) {
    if (Trim(message).empty()) {
        return RunGitCommand({"tag", tag_name});
    }
    return RunGitCommand({"tag", "-a", tag_name, "-m", message});
}

GitManager::CommandResult GitManager::DeleteTag(const std::string& tag_name) {
    return RunGitCommand({"tag", "-d", tag_name});
}

GitManager::CommandResult GitManager::PushTag(const std::string& tag_name) {
    return RunGitCommand({"push", "origin", tag_name});
}

GitManager::CommandResult GitManager::PushAllTags() {
    return RunGitCommand({"push", "origin", "--tags"});
}

GitManager::CommandResult GitManager::FetchTags() {
    return RunGitCommand({"fetch", "--tags", "--prune"});
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

GitManager::CommandResult GitManager::ForcePushWithLease() {
    return RunGitCommand({"push", "--force-with-lease"});
}


std::vector<GitManager::RemoteEntry> GitManager::GetRemotes() {
    std::vector<RemoteEntry> remotes;
    CommandResult result = RunGitCommand({"remote", "-v"});
    if (!result.success()) {
        return remotes;
    }

    std::map<std::string, RemoteEntry> by_name;
    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream line_stream(line);
        std::string name;
        std::string url;
        std::string kind;
        line_stream >> name >> url >> kind;
        if (name.empty() || url.empty()) {
            continue;
        }

        RemoteEntry& entry = by_name[name];
        entry.name = name;
        if (kind.find("push") != std::string::npos) {
            entry.push_url = url;
        } else {
            entry.fetch_url = url;
        }
    }

    for (auto& item : by_name) {
        if (item.second.push_url.empty()) {
            item.second.push_url = item.second.fetch_url;
        }
        if (item.second.fetch_url.empty()) {
            item.second.fetch_url = item.second.push_url;
        }
        remotes.push_back(std::move(item.second));
    }
    return remotes;
}

GitManager::CommandResult GitManager::AddRemote(const std::string& name, const std::string& url) {
    return RunGitCommand({"remote", "add", name, url});
}

GitManager::CommandResult GitManager::SetRemoteUrl(const std::string& name, const std::string& url) {
    return RunGitCommand({"remote", "set-url", name, url});
}

GitManager::CommandResult GitManager::RenameRemote(const std::string& old_name, const std::string& new_name) {
    return RunGitCommand({"remote", "rename", old_name, new_name});
}

GitManager::CommandResult GitManager::RemoveRemote(const std::string& name) {
    return RunGitCommand({"remote", "remove", name});
}

GitManager::CommandResult GitManager::TestRemote(const std::string& name) {
    return RunGitCommand({"ls-remote", "--heads", name});
}

GitManager::GitIdentity GitManager::GetIdentity() {
    GitIdentity identity;
    identity.effective_name = Trim(RunGitCommand({"config", "--get", "user.name"}).output);
    identity.effective_email = Trim(RunGitCommand({"config", "--get", "user.email"}).output);
    identity.local_name = Trim(RunGitCommand({"config", "--local", "--get", "user.name"}).output);
    identity.local_email = Trim(RunGitCommand({"config", "--local", "--get", "user.email"}).output);
    identity.global_name = Trim(RunGitCommand({"config", "--global", "--get", "user.name"}).output);
    identity.global_email = Trim(RunGitCommand({"config", "--global", "--get", "user.email"}).output);
    return identity;
}

GitManager::CommandResult GitManager::SaveLocalIdentity(
    const std::string& name,
    const std::string& email) {
    CommandResult name_result = RunGitCommand({"config", "--local", "user.name", name});
    CommandResult email_result = RunGitCommand({"config", "--local", "user.email", email});
    CommandResult result;
    result.exit_code = name_result.success() && email_result.success() ? 0 : 1;
    result.output = name_result.output + email_result.output;
    return result;
}

GitManager::CommandResult GitManager::SaveGlobalIdentity(
    const std::string& name,
    const std::string& email) {
    CommandResult name_result = RunGitCommand({"config", "--global", "user.name", name});
    CommandResult email_result = RunGitCommand({"config", "--global", "user.email", email});
    CommandResult result;
    result.exit_code = name_result.success() && email_result.success() ? 0 : 1;
    result.output = name_result.output + email_result.output;
    return result;
}

GitManager::CommandResult GitManager::ClearLocalIdentity() {
    CommandResult name_result = RunGitCommand({"config", "--unset", "--local", "user.name"});
    CommandResult email_result = RunGitCommand({"config", "--unset", "--local", "user.email"});
    CommandResult result;
    result.exit_code = 0;
    result.output = name_result.output + email_result.output;
    return result;
}


std::vector<GitManager::CompareRef> GitManager::GetCompareRefs(size_t recent_commit_limit) {
    std::vector<CompareRef> refs;
    std::set<std::string> seen;

    auto add_ref = [&](std::string label, std::string value) {
        label = Trim(std::move(label));
        value = Trim(std::move(value));
        if (label.empty() || value.empty()) {
            return;
        }
        if (!seen.insert(value).second) {
            return;
        }
        refs.push_back({std::move(label), std::move(value)});
    };

    add_ref("Working tree", "WORKTREE");
    add_ref("HEAD", "HEAD");

    std::string current;
    for (const std::string& branch : GetLocalBranches(&current)) {
        add_ref(branch == current ? branch + " (current)" : branch, branch);
    }
    for (const std::string& branch : GetRemoteBranches()) {
        add_ref(branch, branch);
    }
    for (const std::string& tag : GetTags()) {
        add_ref(tag, tag);
    }

    CommandResult log_result = RunGitCommand({
        "log",
        "--all",
        "--date-order",
        "--oneline",
        "-n",
        std::to_string(recent_commit_limit),
    });
    if (log_result.success()) {
        std::istringstream stream(log_result.output);
        std::string line;
        while (std::getline(stream, line)) {
            line = Trim(line);
            if (line.empty()) {
                continue;
            }
            std::istringstream line_stream(line);
            std::string hash;
            line_stream >> hash;
            if (!hash.empty()) {
                add_ref(line, hash);
            }
        }
    }

    return refs;
}

std::vector<GitManager::CompareEntry> GitManager::GetCompareEntries(
    const std::string& left_ref,
    const std::string& right_ref) {
    CommandResult result;
    if (IsWorkingTreeRef(left_ref) && IsWorkingTreeRef(right_ref)) {
        result.exit_code = 0;
        return {};
    }

    if (IsWorkingTreeRef(right_ref)) {
        result = RunGitCommand({"diff", "--name-status", left_ref, "--"});
    } else if (IsWorkingTreeRef(left_ref)) {
        result = RunGitCommand({"diff", "--name-status", "-R", right_ref, "--"});
    } else {
        result = RunGitCommand({"diff", "--name-status", left_ref, right_ref, "--"});
    }

    if (!result.success()) {
        return {};
    }
    return ParseCompareEntries(result.output);
}

GitManager::CommandResult GitManager::CompareDiff(
    const std::string& left_ref,
    const std::string& right_ref,
    const std::string& path) {
    if (IsWorkingTreeRef(left_ref) && IsWorkingTreeRef(right_ref)) {
        CommandResult result;
        result.exit_code = 1;
        result.output = "Both compare refs point to the working tree.";
        return result;
    }

    if (IsWorkingTreeRef(right_ref)) {
        return RunGitCommand({"diff", left_ref, "--", path});
    }
    if (IsWorkingTreeRef(left_ref)) {
        return RunGitCommand({"diff", "-R", right_ref, "--", path});
    }
    return RunGitCommand({"diff", left_ref, right_ref, "--", path});
}

GitManager::CommandResult GitManager::ReadFileAtRef(
    const std::string& ref,
    const std::string& path) {
    if (IsWorkingTreeRef(ref)) {
        CommandResult result;
        const std::filesystem::path root = RepositoryRoot();
        const std::filesystem::path file_path = (root / path).lexically_normal();
        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            result.exit_code = 1;
            result.output = "File is not present in working tree: " + path;
            return result;
        }
        result.output.assign(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
        result.exit_code = 0;
        return result;
    }

    return RunGitCommand({"show", ref + ":" + path});
}

std::vector<std::string> GitManager::GetConfigList(bool global_scope) {
    std::vector<std::string> lines;
    CommandResult result = global_scope
        ? RunGitCommand({"config", "--global", "--list"})
        : RunGitCommand({"config", "--local", "--list"});
    if (!result.success()) {
        return lines;
    }

    std::istringstream stream(result.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
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


std::vector<GitManager::CompareEntry> GitManager::ParseCompareEntries(const std::string& output) {
    std::vector<CompareEntry> entries;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> parts;
        std::string part;
        std::istringstream line_stream(line);
        while (std::getline(line_stream, part, '\t')) {
            parts.push_back(part);
        }
        if (parts.size() < 2) {
            continue;
        }

        CompareEntry entry;
        entry.status = parts[0];
        if (!entry.status.empty() && (entry.status[0] == 'R' || entry.status[0] == 'C') && parts.size() >= 3) {
            entry.old_path = parts[1];
            entry.path = parts[2];
        } else {
            entry.path = parts[1];
        }

        if (!entry.path.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

bool GitManager::IsWorkingTreeRef(const std::string& ref) {
    return Trim(ref).empty() || Trim(ref) == "WORKTREE" || Trim(ref) == "Working tree";
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
