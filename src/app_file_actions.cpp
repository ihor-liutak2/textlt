#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "document.hpp"
#include "text_importer.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {
namespace {

bool IsPlainFolderName(const std::string& name) {
    return !name.empty() &&
           name != "." &&
           name != ".." &&
           name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos &&
           !std::filesystem::path(name).has_parent_path() &&
           !std::filesystem::path(name).is_absolute();
}

} // namespace

    
    
    
bool TextltApp::SaveFile(const std::string& path, std::string& error) {
    try {
        const auto doc = ActiveDocument();
        if (!file_manager_.SaveAs(doc, path, error)) {
            throw std::runtime_error(error);
        }
        RefreshOpenedDocumentsSidebar();
        RefreshProjectSidebar();
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
        git_manager_.Invalidate();
        PersistOpenedDocuments();
        active_action_ = "Saved " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}


bool TextltApp::OpenFile(const std::string& path, std::string& error) {
    try {
        PersistActiveFavoriteCursor();
        const int open_index = FindOpenDocument(path);
        if (open_index >= 0) {
            ActivateOpenDocument(static_cast<size_t>(open_index));
            PersistOpenedDocuments();
            recent_files_history_.AddFile(path);
            active_action_ = "Opened " + path;
            return true;
        }

        auto doc = file_manager_.Open(path, error);
        if (!doc) {
            throw std::runtime_error(error);
        }

        AddOpenDocument(doc);

        RestoreFavoriteCursor(path);
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
        PersistOpenedDocuments();
        recent_files_history_.AddFile(path);
        active_action_ = "Opened " + path;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}


void TextltApp::InitializeWithFiles(const std::vector<std::string>& files_to_open) {
    if (files_to_open.empty()) {
        return;
    }

    const std::filesystem::path active_path = files_to_open.front();
    std::string error;
    if (std::filesystem::exists(active_path)) {
        OpenFile(active_path.string(), error);
    } else {
        auto doc = std::make_shared<Document>();
        doc->Reset();
        doc->SetPath(active_path);
        AddOpenDocument(doc);
        active_action_ = "New file " + active_path.string();
    }

    for (size_t index = 1; index < files_to_open.size(); ++index) {
        const std::filesystem::path path = files_to_open[index];
        if (std::filesystem::exists(path)) {
            OpenFile(path.string(), error);
        } else {
            auto doc = std::make_shared<Document>();
            doc->Reset();
            doc->SetPath(path);
            AddOpenDocument(doc);
        }
    }

    FocusEditor();
}


void TextltApp::OpenSidebarFile(const std::filesystem::path& path) {
    std::string error;
    if (OpenFile(path.string(), error)) {
        FocusEditor();
        screen_.PostEvent(ftxui::Event::Custom);
    }
}


void TextltApp::SaveCurrentFile() {
    const std::string& current_path =
        std::static_pointer_cast<EditorComponent>(text_editor_)->CurrentFilePath();
    if (current_path.empty() || current_path == "Untitled" || current_path == "untitled.txt") {
        OpenFileDialog(FilePromptMode::SaveAs);
        return;
    }

    std::string error;
    SaveFile(current_path, error);
}


void TextltApp::SaveAllOpenedFiles() {
    size_t saved_count = 0;
    size_t skipped_count = 0;
    std::string first_error;

    for (const auto& doc : open_documents_) {
        if (!doc || !doc->is_dirty) {
            continue;
        }

        const std::string path = doc->path.string();
        if (path.empty() || path == "Untitled" || path == "untitled.txt") {
            ++skipped_count;
            continue;
        }

        std::string error;
        if (file_manager_.SaveAs(doc, path, error)) {
            ++saved_count;
        } else if (first_error.empty()) {
            first_error = error;
        }
    }

    RefreshOpenedDocumentsSidebar();
    git_manager_.Invalidate();
    PersistOpenedDocuments();

    if (!first_error.empty()) {
        active_action_ = first_error;
    } else if (skipped_count > 0) {
        active_action_ = "Saved " + std::to_string(saved_count) +
            " file(s); " + std::to_string(skipped_count) + " unsaved draft(s) need Save As";
    } else {
        active_action_ = "Saved " + std::to_string(saved_count) + " file(s)";
    }

    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


bool TextltApp::ConfirmFileDialog(
    FilePromptMode mode,
    const std::string& path,
    std::string& error) {
    bool success = false;
    if (mode == FilePromptMode::Open) {
        std::filesystem::path file_path(path);
        if (!file_path.is_absolute()) {
            file_path = CurrentSidebarDirectory() / file_path;
        }
        success = OpenFile(file_path.string(), error);
    } else if (mode == FilePromptMode::SaveAs) {
        std::filesystem::path file_path(path);
        if (!file_path.is_absolute()) {
            file_path = CurrentSidebarDirectory() / file_path;
        }
        success = SaveFile(file_path.string(), error);
    } else if (mode == FilePromptMode::CreateFolder) {
        success = CreateFolderInCurrentDirectory(path, error);
    } else if (mode == FilePromptMode::DeleteFolder) {
        success = DeleteFolderInCurrentDirectory(path, error);
    } else if (mode == FilePromptMode::DeleteFile) {
        success = DeleteFileInCurrentDirectory(path, error);
    } else {
        error = "No file action selected.";
        return false;
    }

    if (success && mode == FilePromptMode::SaveAs && exit_after_save_as_) {
        exit_after_save_as_ = false;
        PersistActiveFavoriteCursor();
        screen_.Exit();
    }
    return success;
}


bool TextltApp::ConfirmFilesModalAction(
    FilesModalMode mode,
    const std::filesystem::path& path,
    std::string& error) {
    bool success = false;

    if (mode == FilesModalMode::Open) {
        success = OpenFile(path.string(), error);
    } else if (mode == FilesModalMode::SaveAs) {
        success = SaveFile(path.string(), error);
    } else if (mode == FilesModalMode::Import) {
        TextImporter importer;
        TextImportResult result = importer.ImportFile(path);
        if (!result.success) {
            error = result.error.empty() ? "Import failed." : result.error;
            active_action_ = error;
            return false;
        }
        success = InsertImportedText(path, result.text, error);
    } else if (mode == FilesModalMode::Export) {
        const auto doc = ActiveDocument();
        if (!doc) {
            error = "No active document.";
            active_action_ = error;
            return false;
        }

        std::ofstream file(path, std::ios::binary);
        if (!file) {
            error = "Unable to open export file: " + path.string();
            active_action_ = error;
            return false;
        }
        file << doc->ToContent();
        if (!file) {
            error = "Unable to write export file: " + path.string();
            active_action_ = error;
            return false;
        }
        active_action_ = "Exported " + path.string();
        success = true;
    } else {
        error = "No file action selected.";
        return false;
    }

    if (success && mode == FilesModalMode::SaveAs && exit_after_save_as_) {
        exit_after_save_as_ = false;
        PersistActiveFavoriteCursor();
        screen_.Exit();
    }

    if (success) {
        RefreshProjectSidebar();
        git_manager_.Invalidate();
        screen_.PostEvent(ftxui::Event::Custom);
    }
    return success;
}


std::vector<std::filesystem::path> TextltApp::FileModalFavoriteDirectories() const {
    std::vector<std::filesystem::path> directories;
    directories.reserve(editor_config_.file_modal_directories_.size());
    for (const std::string& path : editor_config_.file_modal_directories_) {
        if (!path.empty()) {
            directories.emplace_back(path);
        }
    }
    return directories;
}


bool TextltApp::AddFileModalDirectory(
    const std::filesystem::path& directory,
    std::string& error) {
    const std::string normalized_path =
        EditorConfig::NormalizeDirectoryPath(directory.string());
    if (normalized_path.empty()) {
        error = "Directory path is empty.";
        return false;
    }

    std::error_code status_error;
    if (!std::filesystem::is_directory(normalized_path, status_error)) {
        error = "Directory does not exist.";
        return false;
    }

    if (!editor_config_.IsFileModalDirectory(normalized_path)) {
        editor_config_.file_modal_directories_.push_back(normalized_path);
        SaveConfig();
    }

    active_action_ = "Added file modal directory " + normalized_path;
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


void TextltApp::CopyFileModalPathText(const std::string& text) {
    WriteSystemClipboard(text);
    active_action_ = "Copied path";
    screen_.PostEvent(ftxui::Event::Custom);
}


bool TextltApp::CreateFolderInCurrentDirectory(const std::string& name, std::string& error) {
    if (!IsPlainFolderName(name)) {
        error = "Enter only a folder name.";
        return false;
    }

    const std::filesystem::path folder_name(name);
    const std::filesystem::path target = CurrentSidebarDirectory() / folder_name;
    std::error_code status_error;
    if (std::filesystem::exists(target, status_error)) {
        error = "Folder already exists.";
        return false;
    }

    std::error_code create_error;
    if (!std::filesystem::create_directory(target, create_error) || create_error) {
        error = create_error ? create_error.message() : "Unable to create folder.";
        return false;
    }

    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ = "Created folder " + target.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


bool TextltApp::DeleteFileInCurrentDirectory(const std::string& name, std::string& error) {
    if (!IsPlainFolderName(name)) {
        error = "Enter only a file name.";
        return false;
    }

    const std::filesystem::path file_name(name);
    const std::filesystem::path target = CurrentSidebarDirectory() / file_name;
    std::error_code status_error;
    if (!std::filesystem::is_regular_file(target, status_error)) {
        error = "File does not exist.";
        return false;
    }

    std::error_code remove_error;
    if (!std::filesystem::remove(target, remove_error) || remove_error) {
        error = remove_error ? remove_error.message() : "Unable to delete file.";
        return false;
    }

    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ = "Deleted file " + target.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


bool TextltApp::DeleteFolderInCurrentDirectory(const std::string& name, std::string& error) {
    if (!IsPlainFolderName(name)) {
        error = "Enter only a folder name.";
        return false;
    }

    const std::filesystem::path folder_name(name);
    const std::filesystem::path target = CurrentSidebarDirectory() / folder_name;
    std::error_code status_error;
    if (!std::filesystem::is_directory(target, status_error)) {
        error = "Folder does not exist.";
        return false;
    }

    std::error_code remove_error;
    if (!std::filesystem::remove(target, remove_error) || remove_error) {
        error = remove_error ? remove_error.message() : "Unable to delete folder.";
        return false;
    }

    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ = "Deleted folder " + target.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


std::filesystem::path TextltApp::CurrentSidebarDirectory() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->CurrentPath();
}


std::string TextltApp::SelectedSidebarFileName() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)
        ->GetSelectedFileNameInCurrentDirectory()
        .string();
}


std::string TextltApp::SelectedSidebarPathName() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)
        ->GetSelectedPathInCurrentDirectory()
        .string();
}


std::vector<std::string> TextltApp::CurrentProjectPathCandidates() const {
    std::vector<std::string> candidates;
    const std::filesystem::path root = CurrentSidebarDirectory();
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return candidates;
    }

    std::filesystem::recursive_directory_iterator iter(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && iter != end; iter.increment(error)) {
        const std::filesystem::directory_entry& entry = *iter;
        std::error_code status_error;
        if (!entry.is_directory(status_error) && !entry.is_regular_file(status_error)) {
            continue;
        }

        const std::filesystem::path relative = entry.path().lexically_relative(root);
        if (!relative.empty()) {
            candidates.push_back(relative.generic_string());
        }
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}


bool TextltApp::ResolveProjectRelativePath(
    const std::string& path,
    std::filesystem::path* resolved,
    std::string& error) const {
    if (path.empty()) {
        error = "Enter a path.";
        return false;
    }

    std::filesystem::path relative(path);
    if (relative.is_absolute()) {
        error = "Use a path inside the current folder.";
        return false;
    }

    relative = relative.lexically_normal();
    if (relative.empty() || relative == ".") {
        error = "Enter a path.";
        return false;
    }

    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            error = "Use a path inside the current folder.";
            return false;
        }
    }

    if (resolved) {
        *resolved = (CurrentSidebarDirectory() / relative).lexically_normal();
    }
    return true;
}


void TextltApp::UpdateOpenDocumentPathsAfterMove(
    const std::filesystem::path& from,
    const std::filesystem::path& to) {
    const std::filesystem::path source = std::filesystem::absolute(from).lexically_normal();
    const std::filesystem::path destination = std::filesystem::absolute(to).lexically_normal();
    bool changed = false;

    for (const auto& doc : open_documents_) {
        if (!doc || doc->path.empty() || doc->path == "Untitled" || doc->path == "untitled.txt") {
            continue;
        }

        const std::filesystem::path document_path =
            std::filesystem::absolute(doc->path).lexically_normal();
        if (document_path == source) {
            doc->SetPath(destination);
            changed = true;
            continue;
        }

        const std::filesystem::path relative = document_path.lexically_relative(source);
        if (!relative.empty()) {
            auto iter = relative.begin();
            if (iter != relative.end() && *iter != "..") {
                doc->SetPath(destination / relative);
                changed = true;
            }
        }
    }

    if (changed) {
        RefreshOpenedDocumentsSidebar();
        PersistOpenedDocuments();
    }
}


bool TextltApp::ConfirmPathOperation(
    PathOperationMode mode,
    const std::string& from,
    const std::string& to,
    std::string& error) {
    std::filesystem::path source;
    std::filesystem::path destination;
    if (!ResolveProjectRelativePath(from, &source, error) ||
        !ResolveProjectRelativePath(to, &destination, error)) {
        return false;
    }

    if (source == destination) {
        error = "Source and destination are the same.";
        return false;
    }

    std::error_code status_error;
    if (!std::filesystem::exists(source, status_error)) {
        error = "Source does not exist.";
        return false;
    }
    if (std::filesystem::exists(destination, status_error)) {
        error = "Destination already exists.";
        return false;
    }

    const std::filesystem::path parent = destination.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, status_error)) {
        error = "Destination folder does not exist.";
        return false;
    }

    std::error_code rename_error;
    std::filesystem::rename(source, destination, rename_error);
    if (rename_error) {
        error = rename_error.message();
        return false;
    }

    UpdateOpenDocumentPathsAfterMove(source, destination);
    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ =
        (mode == PathOperationMode::Rename ? "Renamed " : "Moved ") +
        source.filename().string();
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


} // namespace textlt
