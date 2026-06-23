#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace textlt {

struct FileSearchMaskSet {
    std::string name;
    std::string value;
};

struct FileSearchRoot {
    std::filesystem::path path;
    std::string label;
};

struct FileSearchContextLine {
    size_t line_number = 0;
    std::string text;
};

struct FileSearchMatch {
    std::filesystem::path path;
    std::filesystem::path root;
    std::filesystem::path relative_path;

    std::string root_label;

    size_t line_number = 0;
    size_t column = 0;

    std::string line_text;

    std::vector<FileSearchContextLine> before;
    std::vector<FileSearchContextLine> after;
};

struct FileSearchOptions {
    std::vector<FileSearchRoot> roots;

    std::string query;

    FileSearchMaskSet mask_set = {
        "C/C++ Project",
        "*.cpp *.hpp *.h *.cxx *.cc *.c *.hh *.ipp *.inl *.txt *.md *.cmake CMakeLists.txt"
    };

    bool match_case = false;

    size_t context_before = 0;
    size_t context_after = 0;

    size_t max_results = 1000;
    size_t max_file_size_bytes = 2 * 1024 * 1024;

    std::vector<std::string> ignored_directories = {
        ".git",
        ".hg",
        ".svn",
        ".cache",
        ".idea",
        ".vscode",
        "build",
        "cmake-build-debug",
        "cmake-build-release",
        "node_modules",
        "dist",
        "out",
        "target"
    };
};

struct FileSearchSummary {
    std::vector<FileSearchMatch> matches;

    size_t roots_scanned = 0;
    size_t directories_scanned = 0;
    size_t files_scanned = 0;
    size_t files_with_matches = 0;
    size_t files_skipped = 0;

    bool truncated = false;

    std::vector<std::string> errors;

    bool HasErrors() const;
    std::string FirstError() const;
};

class FileSearchEngine final {
public:
    static FileSearchMaskSet DefaultCodeMaskSet();
    static std::vector<FileSearchMaskSet> DefaultMaskSets();

    static std::vector<std::string> ParseMasks(const std::string& masks);

    static bool FileNameMatchesAnyMask(
        const std::filesystem::path& relative_path,
        const std::vector<std::string>& masks);

    FileSearchSummary Search(const FileSearchOptions& options) const;

    FileSearchSummary SearchDirectory(
        const std::filesystem::path& root,
        const std::string& query) const;

    FileSearchSummary SearchDirectory(
        const std::filesystem::path& root,
        const std::string& query,
        const FileSearchMaskSet& mask_set,
        size_t context_before = 0,
        size_t context_after = 0) const;

    FileSearchSummary SearchDirectories(
        const std::vector<std::filesystem::path>& roots,
        const std::string& query,
        const FileSearchMaskSet& mask_set,
        size_t context_before = 0,
        size_t context_after = 0) const;

private:
    void SearchRoot(
        const FileSearchRoot& root,
        const FileSearchOptions& options,
        const std::vector<std::string>& masks,
        FileSearchSummary* summary) const;

    void SearchFile(
        const std::filesystem::path& root,
        const std::string& root_label,
        const std::filesystem::path& file_path,
        const std::filesystem::path& relative_path,
        const FileSearchOptions& options,
        const std::vector<std::string>& masks,
        FileSearchSummary* summary) const;
};

} // namespace textlt
