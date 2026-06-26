#pragma once

#include "document.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace textlt {

enum class FileEntryType {
    Directory,
    File,
    Symlink,
    Other,
};

struct FileEntry {
    std::filesystem::path path;
    std::string name;
    FileEntryType type = FileEntryType::Other;
    std::uintmax_t size = 0;
    bool hidden = false;
};

struct FileFilter {
    std::vector<std::string> extensions;
    bool show_directories = true;
    bool show_files = true;
    bool show_hidden = true;
};

enum class FileClipboardMode {
    None,
    Copy,
    Cut,
};

class FileManager {
public:
    FileManager() = default;

    // Opens an existing file and returns a populated Document.
    std::shared_ptr<Document> Open(const std::filesystem::path& path, std::string& error);

    // Saves the document content to its current path.
    bool Save(std::shared_ptr<Document> doc, std::string& error);

    // Saves the document content to a new path and updates document path/type after success.
    bool SaveAs(std::shared_ptr<Document> doc, const std::filesystem::path& path, std::string& error);

    bool ListDirectory(
        const std::filesystem::path& directory,
        const FileFilter& filter,
        std::vector<FileEntry>& entries,
        std::string& error) const;

    bool CreateDirectory(const std::filesystem::path& path, std::string& error) const;
    bool CreateEmptyFile(const std::filesystem::path& path, std::string& error) const;
    bool DeleteItems(
        const std::vector<std::filesystem::path>& paths,
        bool recursive,
        std::string& error) const;
    bool RenameItem(
        const std::filesystem::path& source,
        const std::string& new_name,
        std::filesystem::path& destination,
        std::string& error) const;
    bool MoveItem(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string& error) const;

    bool CopyItems(const std::vector<std::filesystem::path>& paths, std::string& error);
    bool CutItems(const std::vector<std::filesystem::path>& paths, std::string& error);
    bool PasteItems(
        const std::filesystem::path& destination_directory,
        std::vector<std::filesystem::path>& pasted_paths,
        std::string& error);

    bool CanPaste() const;
    void ClearClipboard();
    FileClipboardMode ClipboardMode() const;
    const std::vector<std::filesystem::path>& ClipboardSources() const;

    static std::filesystem::path UserHomeDirectory();
    static std::filesystem::path UserDocumentsDirectory();
    static std::filesystem::path UserDownloadsDirectory();
    static std::filesystem::path CurrentProcessDirectory();
    static std::filesystem::path ResolvePath(
        const std::string& input,
        const std::filesystem::path& base_directory,
        std::string& error);
    static std::string FormatFileSize(std::uintmax_t size);
    static std::string BuildPathText(const std::vector<std::filesystem::path>& paths);
    static bool IsPlainName(const std::string& name);

private:
    bool StoreClipboard(
        FileClipboardMode mode,
        const std::vector<std::filesystem::path>& paths,
        std::string& error);

    std::vector<std::filesystem::path> clipboard_sources_;
    FileClipboardMode clipboard_mode_ = FileClipboardMode::None;
};

} // namespace textlt
