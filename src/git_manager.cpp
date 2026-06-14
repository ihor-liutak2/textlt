#include "git_manager.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <utility>

namespace textlt {
namespace {

constexpr auto kGitRefreshInterval = std::chrono::milliseconds(1200);

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

std::string GitManager::GetCurrentBranch() {
    RefreshIfNeeded();
    return cached_branch_;
}

std::map<std::string, char> GitManager::GetFileStatuses() {
    RefreshIfNeeded();
    return cached_statuses_;
}

std::filesystem::path GitManager::RepositoryRoot() {
    RefreshIfNeeded();
    return cached_repository_root_;
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
    cached_repository_root_ = std::move(snapshot.repository_root);
}

std::string GitManager::ExecuteCommand(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (status != 0) {
        return "";
    }
    return output;
}

GitManager::Snapshot GitManager::CollectSnapshot(std::filesystem::path working_directory) {
    Snapshot snapshot;
    snapshot.working_directory = working_directory;
    const std::string quoted_directory = ShellQuote(working_directory.string());
    const std::string git_prefix = "git -C " + quoted_directory + " ";

    snapshot.branch = Trim(ExecuteCommand(git_prefix + "branch --show-current 2>/dev/null"));
    snapshot.repository_root = Trim(ExecuteCommand(git_prefix + "rev-parse --show-toplevel 2>/dev/null"));
    snapshot.statuses = ParseStatusOutput(
        ExecuteCommand(git_prefix + "status --porcelain 2>/dev/null"));

    if (snapshot.repository_root.empty()) {
        snapshot.branch.clear();
        snapshot.statuses.clear();
    }

    return snapshot;
}

std::string GitManager::ShellQuote(const std::string& value) {
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
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.size() < 4) {
            continue;
        }

        const char index_status = line[0];
        const char worktree_status = line[1];
        const char status = index_status != ' ' ? index_status : worktree_status;
        std::string path = line.substr(3);

        // Rename entries use "old -> new"; color the current path in the tree.
        const std::string rename_separator = " -> ";
        const size_t rename_position = path.find(rename_separator);
        if (rename_position != std::string::npos) {
            path = path.substr(rename_position + rename_separator.size());
        }

        if (!path.empty()) {
            statuses[path] = status;
        }
    }

    return statuses;
}

} // namespace textlt
