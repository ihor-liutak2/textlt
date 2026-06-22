#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

class RecentFilesHistory {
public:
    static constexpr size_t kMaxEntries = 60;

    struct Entry {
        std::filesystem::path full_path;
        std::filesystem::path folder_path;
        std::string file_name;
    };

    const std::vector<Entry>& Entries() const { return entries_; }
    void AddFile(const std::filesystem::path& path);
    void Load();
    void Refresh();
    bool RemoveFile(const std::filesystem::path& path);

private:
    static std::filesystem::path DefaultPath();
    static std::filesystem::path NormalizePath(const std::filesystem::path& path);
    static Entry MakeEntry(const std::filesystem::path& path);
    bool Save() const;
    bool RemoveMissingFiles();

    std::vector<Entry> entries_;
};

} // namespace textlt
