#include "remote/remote_local_provider.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace textlt {
namespace {

bool CopyDirectoryContents(
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    std::string& error) {
    std::error_code status_error;
    if (!std::filesystem::is_directory(source, status_error)) {
        error = status_error ? status_error.message() : "Source directory does not exist.";
        return false;
    }

    std::error_code create_error;
    std::filesystem::create_directories(target, create_error);
    if (create_error) {
        error = create_error.message();
        return false;
    }

    for (std::filesystem::recursive_directory_iterator iter(source, status_error), end;
         iter != end;
         iter.increment(status_error)) {
        if (status_error) {
            error = status_error.message();
            return false;
        }

        const std::filesystem::path relative = std::filesystem::relative(iter->path(), source, status_error);
        if (status_error) {
            error = status_error.message();
            return false;
        }
        const std::filesystem::path destination = target / relative;

        if (iter->is_directory(status_error)) {
            std::filesystem::create_directories(destination, create_error);
            if (create_error) {
                error = create_error.message();
                return false;
            }
            continue;
        }

        std::filesystem::create_directories(destination.parent_path(), create_error);
        if (create_error) {
            error = create_error.message();
            return false;
        }

        if (iter->is_symlink(status_error)) {
            std::error_code remove_error;
            std::filesystem::remove(destination, remove_error);
            std::error_code copy_error;
            std::filesystem::copy_symlink(iter->path(), destination, copy_error);
            if (copy_error) {
                error = copy_error.message();
                return false;
            }
            continue;
        }

        if (iter->is_regular_file(status_error)) {
            std::error_code copy_error;
            std::filesystem::copy_file(
                iter->path(),
                destination,
                std::filesystem::copy_options::overwrite_existing,
                copy_error);
            if (copy_error) {
                error = copy_error.message();
                return false;
            }
        }
    }

    error.clear();
    return true;
}

} // namespace

RemoteLocalProvider::RemoteLocalProvider(FileManager* file_manager)
    : file_manager_(file_manager) {}

bool RemoteLocalProvider::Connect(const RemoteConnectionConfig& config, std::string& error) {
    error.clear();
    root_ = config.remote_root.empty()
        ? FileManager::CurrentProcessDirectory()
        : FileManager::PathFromUtf8(config.remote_root);
    std::error_code status_error;
    if (root_.empty() || !std::filesystem::is_directory(root_, status_error)) {
        error = "Local directory does not exist: " + FileManager::PathToUtf8(root_);
        return false;
    }
    return true;
}

bool RemoteLocalProvider::TestConnection(std::string& output, std::string& error) {
    std::error_code status_error;
    if (root_.empty() || !std::filesystem::is_directory(root_, status_error)) {
        error = "Local directory is not available.";
        output.clear();
        return false;
    }
    output = "Local path is available: " + FileManager::PathToUtf8(root_);
    error.clear();
    return true;
}

bool RemoteLocalProvider::List(
    const std::string& path,
    std::vector<RemoteEntry>& entries,
    std::string& error) {
    entries.clear();
    if (!file_manager_) {
        error = "File manager is not available.";
        return false;
    }

    const std::filesystem::path directory = Resolve(path);
    FileFilter filter;
    filter.show_directories = true;
    filter.show_files = true;
    filter.show_hidden = true;

    std::vector<FileEntry> local_entries;
    if (!file_manager_->ListDirectory(directory, filter, local_entries, error)) {
        return false;
    }

    for (const FileEntry& entry : local_entries) {
        RemoteEntry remote_entry;
        remote_entry.path = FileManager::PathToUtf8(entry.path);
        remote_entry.name = entry.name;
        remote_entry.size = entry.size;
        remote_entry.hidden = entry.hidden;
        switch (entry.type) {
            case FileEntryType::Directory:
                remote_entry.type = RemoteEntryType::Directory;
                break;
            case FileEntryType::File:
                remote_entry.type = RemoteEntryType::File;
                break;
            case FileEntryType::Symlink:
                remote_entry.type = RemoteEntryType::Symlink;
                break;
            case FileEntryType::Other:
                remote_entry.type = RemoteEntryType::Other;
                break;
        }
        entries.push_back(std::move(remote_entry));
    }
    error.clear();
    return true;
}

bool RemoteLocalProvider::Download(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    std::error_code copy_error;
    std::filesystem::copy_file(
        Resolve(remote_path),
        FileManager::PathFromUtf8(local_path),
        std::filesystem::copy_options::overwrite_existing,
        copy_error);
    if (copy_error) {
        error = copy_error.message();
        return false;
    }
    error.clear();
    return true;
}

bool RemoteLocalProvider::Upload(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    std::error_code copy_error;
    std::filesystem::copy_file(
        FileManager::PathFromUtf8(local_path),
        Resolve(remote_path),
        std::filesystem::copy_options::overwrite_existing,
        copy_error);
    if (copy_error) {
        error = copy_error.message();
        return false;
    }
    error.clear();
    return true;
}

bool RemoteLocalProvider::DownloadDirectory(
    const std::string& remote_path,
    const std::string& local_path,
    std::string& error) {
    return CopyDirectoryContents(Resolve(remote_path), FileManager::PathFromUtf8(local_path), error);
}

bool RemoteLocalProvider::UploadDirectory(
    const std::string& local_path,
    const std::string& remote_path,
    std::string& error) {
    return CopyDirectoryContents(FileManager::PathFromUtf8(local_path), Resolve(remote_path), error);
}

bool RemoteLocalProvider::Rename(
    const std::string& old_path,
    const std::string& new_path,
    std::string& error) {
    std::error_code rename_error;
    std::filesystem::rename(Resolve(old_path), Resolve(new_path), rename_error);
    if (rename_error) {
        error = rename_error.message();
        return false;
    }
    error.clear();
    return true;
}

bool RemoteLocalProvider::RemoveFile(const std::string& path, std::string& error) {
    std::error_code remove_error;
    if (!std::filesystem::remove(Resolve(path), remove_error) || remove_error) {
        error = remove_error ? remove_error.message() : "File was not removed.";
        return false;
    }
    error.clear();
    return true;
}

bool RemoteLocalProvider::MakeDirectory(const std::string& path, std::string& error) {
    std::error_code create_error;
    if (!std::filesystem::create_directories(Resolve(path), create_error) && create_error) {
        error = create_error.message();
        return false;
    }
    error.clear();
    return true;
}

bool RemoteLocalProvider::RemoveDirectory(const std::string& path, std::string& error) {
    std::error_code remove_error;
    if (!std::filesystem::remove(Resolve(path), remove_error) || remove_error) {
        error = remove_error ? remove_error.message() : "Directory was not removed.";
        return false;
    }
    error.clear();
    return true;
}

std::filesystem::path RemoteLocalProvider::Resolve(const std::string& path) const {
    if (path.empty()) {
        return root_;
    }
    const std::filesystem::path parsed = FileManager::PathFromUtf8(path);
    if (parsed.is_absolute()) {
        return parsed;
    }
    return root_ / parsed;
}

} // namespace textlt
