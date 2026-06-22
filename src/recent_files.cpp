#include "recent_files.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "json_utils.hpp"

namespace textlt {
namespace {

constexpr const char* kRecentFilesKey = "recent_files";

} // namespace

void RecentFilesHistory::AddFile(const std::filesystem::path& path) {
    const std::filesystem::path normalized_path = NormalizePath(path);
    if (normalized_path.empty()) {
        return;
    }

    Entry entry = MakeEntry(normalized_path);

    entries_.erase(
        std::remove_if(
            entries_.begin(),
            entries_.end(),
            [&entry](const Entry& existing) {
                return existing.full_path == entry.full_path;
            }),
        entries_.end());
    entries_.insert(entries_.begin(), std::move(entry));

    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }
    Save();
}

void RecentFilesHistory::Load() {
    entries_.clear();

    const Json root = LoadJsonObject(DefaultPath());
    const auto files = root.find(kRecentFilesKey);
    if (files == root.end() || !files->is_array()) {
        return;
    }

    bool changed = false;
    for (const Json& item : *files) {
        std::string path;
        if (item.is_string()) {
            path = item.get<std::string>();
        } else if (item.is_object()) {
            path = JsonString(item, "path");
        }
        const std::filesystem::path normalized_path = NormalizePath(path);
        if (normalized_path.empty()) {
            changed = true;
            continue;
        }
        const Entry entry = MakeEntry(normalized_path);
        if (std::find_if(
                entries_.begin(),
                entries_.end(),
                [&entry](const Entry& existing) {
                    return existing.full_path == entry.full_path;
                }) == entries_.end()) {
            entries_.push_back(entry);
        } else {
            changed = true;
        }
        if (entries_.size() >= kMaxEntries) {
            changed = true;
            break;
        }
    }

    if (RemoveMissingFiles()) {
        changed = true;
    }
    if (changed) {
        Save();
    }
}

void RecentFilesHistory::Refresh() {
    if (RemoveMissingFiles()) {
        Save();
    }
}

bool RecentFilesHistory::RemoveFile(const std::filesystem::path& path) {
    const std::filesystem::path normalized_path = NormalizePath(path);
    if (normalized_path.empty()) {
        return false;
    }

    const size_t old_size = entries_.size();
    entries_.erase(
        std::remove_if(
            entries_.begin(),
            entries_.end(),
            [&normalized_path](const Entry& entry) {
                return entry.full_path == normalized_path;
            }),
        entries_.end());
    if (entries_.size() == old_size) {
        return false;
    }
    Save();
    return true;
}

std::filesystem::path RecentFilesHistory::DefaultPath() {
#ifdef _WIN32
    const char* app_data = std::getenv("APPDATA");
    if (app_data && !std::string(app_data).empty()) {
        return std::filesystem::path(app_data) / "textlt" / "recent_files.json";
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && !std::string(user_profile).empty()) {
        return std::filesystem::path(user_profile) / "AppData" / "Roaming" /
               "textlt" / "recent_files.json";
    }
    return "recent_files.json";
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return "recent_files.json";
    }
    return std::filesystem::path(home) / ".config" / "textlt" / "recent_files.json";
#endif
}

std::filesystem::path RecentFilesHistory::NormalizePath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    std::filesystem::path normalized_path = std::filesystem::absolute(path, error);
    if (error) {
        return {};
    }

    normalized_path = std::filesystem::weakly_canonical(normalized_path, error);
    if (error) {
        normalized_path = normalized_path.lexically_normal();
    }
    return normalized_path;
}

RecentFilesHistory::Entry RecentFilesHistory::MakeEntry(const std::filesystem::path& path) {
    Entry entry;
    entry.full_path = path;
    entry.folder_path = path.parent_path();
    entry.file_name = path.filename().string();
    return entry;
}

bool RecentFilesHistory::Save() const {
    Json root = Json::object();
    root[kRecentFilesKey] = Json::array();
    for (const Entry& entry : entries_) {
        root[kRecentFilesKey].push_back({{"path", entry.full_path.string()}});
    }
    return WriteJsonAtomically(DefaultPath(), root);
}

bool RecentFilesHistory::RemoveMissingFiles() {
    const size_t old_size = entries_.size();
    entries_.erase(
        std::remove_if(
            entries_.begin(),
            entries_.end(),
            [](const Entry& entry) {
                std::error_code error;
                return !std::filesystem::is_regular_file(entry.full_path, error);
            }),
        entries_.end());
    return entries_.size() != old_size;
}

} // namespace textlt
