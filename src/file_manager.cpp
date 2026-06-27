#include "file_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace textlt {
namespace {

std::string ReadFileContent(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
}

void WriteFileContent(const std::filesystem::path& path, const std::string& content) {
    if (path.empty() || path == "Untitled") {
        throw std::runtime_error("No file path selected.");
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open file for writing: " + path.string());
    }

    file << content;
    if (!file) {
        throw std::runtime_error("Unable to write file: " + path.string());
    }
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string PathComparisonKey(const std::filesystem::path& path) {
    std::string key = path.string();
#ifdef _WIN32
    return Lowercase(key);
#else
    return key;
#endif
}

#ifdef _WIN32
std::filesystem::path WindowsKnownFolder(const KNOWNFOLDERID& folder_id) {
    PWSTR raw_path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(folder_id, KF_FLAG_DEFAULT, nullptr, &raw_path);
    if (FAILED(result) || raw_path == nullptr) {
        return {};
    }

    std::filesystem::path path(raw_path);
    CoTaskMemFree(raw_path);
    return path;
}

bool WindowsHiddenAttribute(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.wstring().c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
}

bool HasWindowsReservedName(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    for (unsigned char character : name) {
        if (character < 32) {
            return true;
        }
    }

    static const std::string invalid_characters = "<>:\"|?*";
    if (name.find_first_of(invalid_characters) != std::string::npos) {
        return true;
    }

    if (name.back() == ' ' || name.back() == '.') {
        return true;
    }

    const std::string stem = Lowercase(std::filesystem::path(name).stem().string());
    static const char* reserved_names[] = {
        "con",
        "prn",
        "aux",
        "nul",
        "conin$",
        "conout$",
        "com1",
        "com2",
        "com3",
        "com4",
        "com5",
        "com6",
        "com7",
        "com8",
        "com9",
        "lpt1",
        "lpt2",
        "lpt3",
        "lpt4",
        "lpt5",
        "lpt6",
        "lpt7",
        "lpt8",
        "lpt9",
    };
    for (const char* reserved_name : reserved_names) {
        if (stem == reserved_name) {
            return true;
        }
    }
    return false;
}
#endif

std::filesystem::path NormalizeExistingPath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path absolute_path = std::filesystem::absolute(path, error);
    if (error) {
        return path.lexically_normal();
    }

    std::filesystem::path canonical_path = std::filesystem::weakly_canonical(absolute_path, error);
    if (error) {
        return absolute_path.lexically_normal();
    }
    return canonical_path;
}

std::filesystem::path ExpandUserPath(const std::string& input) {
    if (input.empty() || input[0] != '~') {
        return std::filesystem::path(input);
    }

    const std::filesystem::path home = FileManager::UserHomeDirectory();
    if (home.empty()) {
        return std::filesystem::path(input);
    }

    if (input.size() == 1) {
        return home;
    }

    if (input[1] == '/' || input[1] == '\\') {
        return home / input.substr(2);
    }

    return std::filesystem::path(input);
}

bool IsRootLikePath(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute_path = std::filesystem::absolute(path, error);
    if (error) {
        return false;
    }

#ifdef _WIN32
    // Protect drive roots such as C:\ and UNC roots such as \\server\share\.
    if (absolute_path == absolute_path.root_path()) {
        return true;
    }
    return absolute_path.has_root_name() && absolute_path.parent_path() == absolute_path;
#else
    return absolute_path == absolute_path.root_path();
#endif
}

bool HasExtensionAllowed(const std::filesystem::path& path, const FileFilter& filter) {
    if (filter.extensions.empty()) {
        return true;
    }

    const std::string extension = Lowercase(path.extension().string());
    for (std::string allowed : filter.extensions) {
        allowed = Lowercase(std::move(allowed));
        if (!allowed.empty() && allowed[0] != '.') {
            allowed.insert(allowed.begin(), '.');
        }
        if (extension == allowed) {
            return true;
        }
    }
    return false;
}

FileEntryType EntryType(const std::filesystem::directory_entry& entry) {
    std::error_code error;
    if (entry.is_directory(error)) {
        return FileEntryType::Directory;
    }
    if (entry.is_regular_file(error)) {
        return FileEntryType::File;
    }
    if (entry.is_symlink(error)) {
        return FileEntryType::Symlink;
    }
    return FileEntryType::Other;
}

bool IsDirectoryPath(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

bool IsInsidePath(const std::filesystem::path& candidate, const std::filesystem::path& parent) {
    const std::filesystem::path normalized_candidate = NormalizeExistingPath(candidate);
    const std::filesystem::path normalized_parent = NormalizeExistingPath(parent);
    if (PathComparisonKey(normalized_candidate) == PathComparisonKey(normalized_parent)) {
        return true;
    }

    const std::filesystem::path relative = normalized_candidate.lexically_relative(normalized_parent);
    if (relative.empty()) {
        return true;
    }
    auto iter = relative.begin();
    return iter != relative.end() && *iter != "..";
}

bool ValidateSourceList(
    const std::vector<std::filesystem::path>& paths,
    std::vector<std::filesystem::path>& normalized_paths,
    std::string& error) {
    if (paths.empty()) {
        error = "No files or directories selected.";
        return false;
    }

    std::set<std::string> seen;
    for (const std::filesystem::path& path : paths) {
        if (path.empty()) {
            continue;
        }
        std::error_code status_error;
        if (!std::filesystem::exists(path, status_error)) {
            error = "Path does not exist: " + path.string();
            return false;
        }
        const std::filesystem::path normalized_path = NormalizeExistingPath(path);
        if (IsRootLikePath(normalized_path)) {
            error = "Refusing to operate on filesystem root.";
            return false;
        }
        const std::string key = PathComparisonKey(normalized_path);
        if (seen.insert(key).second) {
            normalized_paths.push_back(normalized_path);
        }
    }

    if (normalized_paths.empty()) {
        error = "No valid files or directories selected.";
        return false;
    }
    return true;
}

bool CopyOne(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string& error) {
    std::error_code status_error;
    if (std::filesystem::exists(destination, status_error)) {
        error = "Destination already exists: " + destination.string();
        return false;
    }

    if (std::filesystem::is_directory(source, status_error)) {
        std::filesystem::copy(
            source,
            destination,
            std::filesystem::copy_options::recursive,
            status_error);
        if (status_error) {
            error = status_error.message();
            return false;
        }
        return true;
    }

    if (std::filesystem::is_symlink(source, status_error)) {
        std::filesystem::copy_symlink(source, destination, status_error);
        if (status_error) {
            error = status_error.message();
            return false;
        }
        return true;
    }

    if (std::filesystem::is_regular_file(source, status_error)) {
        const std::filesystem::path parent = destination.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, status_error);
            if (status_error) {
                error = status_error.message();
                return false;
            }
        }
        std::filesystem::copy_file(source, destination, status_error);
        if (status_error) {
            error = status_error.message();
            return false;
        }
        return true;
    }

    error = "Unsupported file type: " + source.string();
    return false;
}

bool RemoveOne(const std::filesystem::path& path, bool recursive, std::string& error) {
    std::error_code status_error;
    if (!std::filesystem::exists(path, status_error)) {
        error = "Path does not exist: " + path.string();
        return false;
    }

    if (IsRootLikePath(path)) {
        error = "Refusing to delete filesystem root.";
        return false;
    }

    std::error_code remove_error;
    if (std::filesystem::is_directory(path, status_error) && recursive) {
        std::filesystem::remove_all(path, remove_error);
        if (remove_error) {
            error = remove_error.message();
            return false;
        }
        return true;
    }

    if (!std::filesystem::remove(path, remove_error) || remove_error) {
        error = remove_error ? remove_error.message() : "Unable to delete path.";
        return false;
    }
    return true;
}

bool MoveOne(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string& error) {
    std::error_code status_error;
    if (!std::filesystem::exists(source, status_error)) {
        error = "Source does not exist: " + source.string();
        return false;
    }
    if (std::filesystem::exists(destination, status_error)) {
        error = "Destination already exists: " + destination.string();
        return false;
    }
    if (IsRootLikePath(source)) {
        error = "Refusing to move filesystem root.";
        return false;
    }
    if (std::filesystem::is_directory(source, status_error) && IsInsidePath(destination, source)) {
        error = "Cannot move a directory into itself.";
        return false;
    }

    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, status_error);
        if (status_error) {
            error = status_error.message();
            return false;
        }
    }

    std::error_code rename_error;
    std::filesystem::rename(source, destination, rename_error);
    if (!rename_error) {
        return true;
    }

    if (!CopyOne(source, destination, error)) {
        if (error.empty()) {
            error = rename_error.message();
        }
        return false;
    }

    if (!RemoveOne(source, true, error)) {
        error = "Moved copy was created, but source could not be removed: " + error;
        return false;
    }
    return true;
}

} // namespace

std::shared_ptr<Document> FileManager::Open(const std::filesystem::path& path, std::string& error) {
    try {
        auto doc = std::make_shared<Document>(path);
        doc->LoadContent(ReadFileContent(path), path);
        return doc;
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
}

bool FileManager::Save(std::shared_ptr<Document> doc, std::string& error) {
    if (!doc) {
        error = "Document is null.";
        return false;
    }

    return SaveAs(doc, doc->path, error);
}

bool FileManager::SaveAs(
    std::shared_ptr<Document> doc,
    const std::filesystem::path& path,
    std::string& error) {
    if (!doc) {
        error = "Document is null.";
        return false;
    }

    try {
        WriteFileContent(path, doc->ToContent());
        doc->SetPath(path);
        doc->is_dirty = false;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool FileManager::ListDirectory(
    const std::filesystem::path& directory,
    const FileFilter& filter,
    std::vector<FileEntry>& entries,
    std::string& error) const {
    entries.clear();

    std::error_code status_error;
    if (!std::filesystem::is_directory(directory, status_error)) {
        error = "Directory does not exist: " + directory.string();
        return false;
    }

    std::filesystem::directory_iterator iterator(
        directory,
        std::filesystem::directory_options::skip_permission_denied,
        status_error);
    if (status_error) {
        error = status_error.message();
        return false;
    }

    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(status_error)) {
        if (status_error) {
            error = status_error.message();
            return false;
        }
        const std::filesystem::directory_entry& entry = *iterator;
        const std::string name = entry.path().filename().string();
        bool hidden = !name.empty() && name[0] == '.';
#ifdef _WIN32
        hidden = hidden || WindowsHiddenAttribute(entry.path());
#endif
        if (hidden && !filter.show_hidden) {
            continue;
        }

        const FileEntryType type = EntryType(entry);
        const bool directory_entry = type == FileEntryType::Directory;
        const bool file_entry = type == FileEntryType::File || type == FileEntryType::Symlink;
        if (directory_entry && !filter.show_directories) {
            continue;
        }
        if (!directory_entry && (!file_entry || !filter.show_files)) {
            continue;
        }
        if (!directory_entry && !HasExtensionAllowed(entry.path(), filter)) {
            continue;
        }

        FileEntry file_entry_info;
        file_entry_info.path = entry.path();
        file_entry_info.name = name;
        file_entry_info.type = type;
        file_entry_info.hidden = hidden;
        if (type == FileEntryType::File) {
            std::error_code size_error;
            file_entry_info.size = entry.file_size(size_error);
            if (size_error) {
                file_entry_info.size = 0;
            }
        }
        entries.push_back(std::move(file_entry_info));
    }

    std::sort(entries.begin(), entries.end(), [](const FileEntry& left, const FileEntry& right) {
        if (left.type == FileEntryType::Directory && right.type != FileEntryType::Directory) {
            return true;
        }
        if (left.type != FileEntryType::Directory && right.type == FileEntryType::Directory) {
            return false;
        }
        return Lowercase(left.name) < Lowercase(right.name);
    });
    return true;
}

bool FileManager::CreateDirectoryItem(const std::filesystem::path& path, std::string& error) const {
    if (path.empty()) {
        error = "Enter a directory path.";
        return false;
    }
    std::error_code status_error;
    if (std::filesystem::exists(path, status_error)) {
        error = "Path already exists: " + path.string();
        return false;
    }
    std::filesystem::create_directories(path, status_error);
    if (status_error) {
        error = status_error.message();
        return false;
    }
    return true;
}

bool FileManager::CreateEmptyFile(const std::filesystem::path& path, std::string& error) const {
    if (path.empty()) {
        error = "Enter a file path.";
        return false;
    }
    std::error_code status_error;
    if (std::filesystem::exists(path, status_error)) {
        error = "File already exists: " + path.string();
        return false;
    }
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, status_error);
        if (status_error) {
            error = status_error.message();
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Unable to create file: " + path.string();
        return false;
    }
    return true;
}

bool FileManager::DeleteItems(
    const std::vector<std::filesystem::path>& paths,
    bool recursive,
    std::string& error) const {
    std::vector<std::filesystem::path> normalized_paths;
    if (!ValidateSourceList(paths, normalized_paths, error)) {
        return false;
    }

    for (const std::filesystem::path& path : normalized_paths) {
        if (!RemoveOne(path, recursive, error)) {
            return false;
        }
    }
    return true;
}

bool FileManager::RenameItem(
    const std::filesystem::path& source,
    const std::string& new_name,
    std::filesystem::path& destination,
    std::string& error) const {
    if (!IsPlainName(new_name)) {
        error = "Enter only a file or directory name.";
        return false;
    }

    std::error_code status_error;
    if (!std::filesystem::exists(source, status_error)) {
        error = "Source does not exist: " + source.string();
        return false;
    }

    destination = source.parent_path() / new_name;
    return MoveOne(source, destination, error);
}

bool FileManager::MoveItem(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string& error) const {
    std::filesystem::path effective_destination = destination;
    if (IsDirectoryPath(destination)) {
        effective_destination = destination / source.filename();
    }
    return MoveOne(source, effective_destination, error);
}

bool FileManager::CopyItems(const std::vector<std::filesystem::path>& paths, std::string& error) {
    return StoreClipboard(FileClipboardMode::Copy, paths, error);
}

bool FileManager::CutItems(const std::vector<std::filesystem::path>& paths, std::string& error) {
    return StoreClipboard(FileClipboardMode::Cut, paths, error);
}

bool FileManager::PasteItems(
    const std::filesystem::path& destination_directory,
    std::vector<std::filesystem::path>& pasted_paths,
    std::string& error) {
    pasted_paths.clear();
    if (!CanPaste()) {
        error = "Clipboard is empty.";
        return false;
    }

    std::error_code status_error;
    if (!std::filesystem::is_directory(destination_directory, status_error)) {
        error = "Paste destination is not a directory.";
        return false;
    }

    for (const std::filesystem::path& source : clipboard_sources_) {
        if (!std::filesystem::exists(source, status_error)) {
            error = "Clipboard source no longer exists: " + source.string();
            return false;
        }

        const std::filesystem::path destination = destination_directory / source.filename();
        if (std::filesystem::exists(destination, status_error)) {
            error = "Destination already exists: " + destination.string();
            return false;
        }
        if (std::filesystem::is_directory(source, status_error) &&
            IsInsidePath(destination_directory, source)) {
            error = "Cannot paste a directory into itself.";
            return false;
        }
    }

    for (const std::filesystem::path& source : clipboard_sources_) {
        const std::filesystem::path destination = destination_directory / source.filename();
        const bool success = clipboard_mode_ == FileClipboardMode::Copy
            ? CopyOne(source, destination, error)
            : MoveOne(source, destination, error);
        if (!success) {
            return false;
        }
        pasted_paths.push_back(destination);
    }

    if (clipboard_mode_ == FileClipboardMode::Cut) {
        ClearClipboard();
    }
    return true;
}

bool FileManager::CanPaste() const {
    return clipboard_mode_ != FileClipboardMode::None && !clipboard_sources_.empty();
}

void FileManager::ClearClipboard() {
    clipboard_sources_.clear();
    clipboard_mode_ = FileClipboardMode::None;
}

FileClipboardMode FileManager::ClipboardMode() const {
    return clipboard_mode_;
}

const std::vector<std::filesystem::path>& FileManager::ClipboardSources() const {
    return clipboard_sources_;
}

std::filesystem::path FileManager::UserHomeDirectory() {
#ifdef _WIN32
    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile && std::string(user_profile).size() > 0) {
        return std::filesystem::path(user_profile);
    }
    const char* home_drive = std::getenv("HOMEDRIVE");
    const char* home_path = std::getenv("HOMEPATH");
    if (home_drive && home_path) {
        return std::filesystem::path(std::string(home_drive) + std::string(home_path));
    }
    return {};
#else
    const char* home = std::getenv("HOME");
    if (!home || std::string(home).empty()) {
        return {};
    }
    return std::filesystem::path(home);
#endif
}

std::filesystem::path FileManager::UserDocumentsDirectory() {
#ifdef _WIN32
    const std::filesystem::path known_folder = WindowsKnownFolder(FOLDERID_Documents);
    if (!known_folder.empty()) {
        std::error_code known_folder_error;
        if (std::filesystem::is_directory(known_folder, known_folder_error)) {
            return known_folder;
        }
    }
#endif

    const std::filesystem::path home = UserHomeDirectory();
    if (home.empty()) {
        return CurrentProcessDirectory();
    }
    const std::filesystem::path documents = home / "Documents";
    std::error_code error;
    return std::filesystem::is_directory(documents, error) ? documents : home;
}

std::filesystem::path FileManager::UserDownloadsDirectory() {
#ifdef _WIN32
    const std::filesystem::path known_folder = WindowsKnownFolder(FOLDERID_Downloads);
    if (!known_folder.empty()) {
        std::error_code known_folder_error;
        if (std::filesystem::is_directory(known_folder, known_folder_error)) {
            return known_folder;
        }
    }
#endif

    const std::filesystem::path home = UserHomeDirectory();
    if (home.empty()) {
        return CurrentProcessDirectory();
    }
    const std::filesystem::path downloads = home / "Downloads";
    std::error_code error;
    return std::filesystem::is_directory(downloads, error) ? downloads : home;
}

std::filesystem::path FileManager::CurrentProcessDirectory() {
    std::error_code error;
    std::filesystem::path current = std::filesystem::current_path(error);
    return error ? std::filesystem::path(".") : current;
}

std::filesystem::path FileManager::ResolvePath(
    const std::string& input,
    const std::filesystem::path& base_directory,
    std::string& error) {
    if (input.empty()) {
        error = "Enter a path.";
        return {};
    }

    std::filesystem::path path = ExpandUserPath(input);
    if (!path.is_absolute()) {
        path = base_directory / path;
    }
    return NormalizeExistingPath(path);
}

std::string FileManager::FormatFileSize(std::uintmax_t size) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(size);
    size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit_index;
    }

    char buffer[64];
    if (unit_index == 0) {
        std::snprintf(buffer, sizeof(buffer), "%llu %s", static_cast<unsigned long long>(size), units[unit_index]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit_index]);
    }
    return buffer;
}

std::string FileManager::BuildPathText(const std::vector<std::filesystem::path>& paths) {
    std::ostringstream output;
    bool first = true;
    for (const std::filesystem::path& path : paths) {
        if (!first) {
            output << '\n';
        }
        first = false;
        output << NormalizeExistingPath(path).string();
    }
    return output.str();
}

bool FileManager::IsPlainName(const std::string& name) {
    if (name.empty() ||
        name == "." ||
        name == ".." ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos ||
        std::filesystem::path(name).has_parent_path() ||
        std::filesystem::path(name).is_absolute()) {
        return false;
    }

#ifdef _WIN32
    return !HasWindowsReservedName(name);
#else
    return true;
#endif
}

bool FileManager::StoreClipboard(
    FileClipboardMode mode,
    const std::vector<std::filesystem::path>& paths,
    std::string& error) {
    std::vector<std::filesystem::path> normalized_paths;
    if (!ValidateSourceList(paths, normalized_paths, error)) {
        return false;
    }

    clipboard_mode_ = mode;
    clipboard_sources_ = std::move(normalized_paths);
    return true;
}

} // namespace textlt
