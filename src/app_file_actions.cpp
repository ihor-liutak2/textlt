#include "app.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "document.hpp"
#include "text_importer.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {
    
    
    
bool TextltApp::SaveFile(const std::string& path, std::string& error) {
    try {
        if (!document_file_controller_.SaveActiveDocumentAs(path, error)) {
            throw std::runtime_error(error);
        }
        RefreshOpenedDocumentsSidebar();
        RefreshProjectSidebar();
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
        git_manager_.Invalidate();

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
        if (!document_file_controller_.OpenDocument(path, error)) {
            throw std::runtime_error(error);
        }

        BindEditorComponentsToWorkspace();
        RefreshOpenedDocumentsSidebar();
        UpdateFileMenuLabels();
        git_manager_.SetWorkingDirectory(std::filesystem::path(path).parent_path());
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

    std::string error;
    for (size_t index = 0; index < files_to_open.size(); ++index) {
        const std::filesystem::path path = files_to_open[index];
        if (!document_file_controller_.OpenOrCreateDocument(path, error)) {
            active_action_ = error.empty() ? "Could not open " + path.string() : error;
            continue;
        }
        active_action_ = std::filesystem::exists(path)
            ? "Opened " + path.string()
            : "New file " + path.string();
    }

    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
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
    const auto document = document_workspace_.ActiveDocument();
    if (!document || DocumentWorkspace::IsMemoryOnlyDocument(document)) {
        OpenFilesModal(FilesModalMode::SaveAs);
        return;
    }

    std::string error;
    if (!document_file_controller_.SaveActiveDocument(error)) {
        active_action_ = error.empty() ? "Save failed" : error;
        return;
    }

    RefreshOpenedDocumentsSidebar();
    RefreshProjectSidebar();
    git_manager_.SetWorkingDirectory(document->path.parent_path());
    git_manager_.Invalidate();
    active_action_ = "Saved " + document->path.string();
}


void TextltApp::SaveAllOpenedFiles() {
    const auto result = document_file_controller_.SaveAllDirtyDocuments();

    RefreshOpenedDocumentsSidebar();
    git_manager_.Invalidate();

    if (!result.first_error.empty()) {
        active_action_ = result.first_error;
    } else if (result.skipped_count > 0) {
        active_action_ = "Saved " + std::to_string(result.saved_count) +
            " file(s); " + std::to_string(result.skipped_count) + " unsaved draft(s) need Save As";
    } else {
        active_action_ = "Saved " + std::to_string(result.saved_count) + " file(s)";
    }

    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
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
    clipboard_controller_.WriteText(text);
    active_action_ = "Copied path";
    screen_.PostEvent(ftxui::Event::Custom);
}


std::filesystem::path TextltApp::CurrentSidebarDirectory() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->CurrentPath();
}


} // namespace textlt
