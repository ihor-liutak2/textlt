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
    
    
    
bool TextltApp::SaveFile(const std::string& path, std::string& error) {
    try {
        const auto doc = ActiveDocument();
        if (doc && doc->read_only) {
            error = "Cannot save a read-only Git compare document.";
            throw std::runtime_error(error);
        }
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
        OpenFilesModal(FilesModalMode::SaveAs);
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


std::filesystem::path TextltApp::CurrentSidebarDirectory() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->CurrentPath();
}


} // namespace textlt
