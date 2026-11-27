#include "app.hpp"

#include "connect_workspace.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "editor/document_session.hpp"
#include "actions/action_config_importer.hpp"
#include "notes/markdown_importer.hpp"
#include "path_utils.hpp"
#include "text_importer.hpp"
#include "ftxui/component/event.hpp"
#include "modals/delete_confirmation_popup.hpp"

namespace textlt {
    
    
    
bool TextltApp::SaveFile(const std::shared_ptr<DocumentSession>& document,
    const std::filesystem::path& path, std::string& error) {
    try {
        if (!document_file_controller_.SaveSessionAs(document, path, error)) {
            throw std::runtime_error(error);
        }
        RefreshProjectSidebar();
        git_manager_.Invalidate();

        active_action_ = "Saved " + Utf8FromPath(path);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        active_action_ = error;
        return false;
    }
}


bool TextltApp::OpenFile(const std::string& path, std::string& error) {
    try {
        if (!document_file_controller_.OpenSession(PathFromUtf8(path), error)) {
            throw std::runtime_error(error);
        }

        CloseGitCompare();
        BindEditorComponentsToWorkspace();
        UpdateFileMenuLabels();
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
        const std::filesystem::path path = PathFromUtf8(files_to_open[index]);
        if (!document_file_controller_.OpenOrCreateSession(path, error)) {
            active_action_ = error.empty() ? "Could not open " + Utf8FromPath(path) : error;
            continue;
        }
        active_action_ = std::filesystem::exists(path)
            ? "Opened " + Utf8FromPath(path)
            : "New file " + Utf8FromPath(path);
    }

    BindEditorComponentsToWorkspace();
    UpdateFileMenuLabels();
    FocusEditor();
}


void TextltApp::OpenSidebarFile(const std::filesystem::path& path) {
    std::string error;
    if (OpenFile(Utf8FromPath(path), error)) {
        FocusEditor();
        screen_.PostEvent(ftxui::Event::Custom);
    }
}

void TextltApp::OpenRenameSidebarItem(const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        active_action_ = error ? error.message() : "The selected item no longer exists.";
        NotifyWarning("The selected item no longer exists.");
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    sidebar_item_operation_path_ = path;
    sidebar_item_name_ = FileManager::PathToUtf8(path.filename());
    sidebar_item_operation_error_.clear();
    *sidebar_item_name_selection_ = {};
    sidebar_item_name_selection_->cursor = static_cast<int>(sidebar_item_name_.size());
    if (menu_bar_) menu_bar_->CloseDropdown();
    SetActiveLayer(UiLayer::RenameSidebarItem);
    sidebar_item_name_input_->TakeFocus();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::SubmitRenameSidebarItem() {
    if (sidebar_item_operation_path_.empty()) return;
    if (!FileManager::IsPlainName(sidebar_item_name_)) {
        sidebar_item_operation_error_ = "Enter a valid file or folder name.";
        NotifyWarning("Enter a valid file or folder name.");
        sidebar_item_name_input_->TakeFocus();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    std::filesystem::path destination;
    std::string error;
    std::error_code type_error;
    const bool was_directory =
        std::filesystem::is_directory(sidebar_item_operation_path_, type_error);
    if (!file_manager_.RenameItem(
            sidebar_item_operation_path_, sidebar_item_name_, destination, error)) {
        sidebar_item_operation_error_ = error.empty() ? "Rename failed." : error;
        NotifyError("Could not rename item.");
        sidebar_item_name_input_->TakeFocus();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    const bool was_favorite =
        document_file_controller_.IsFavorite(sidebar_item_operation_path_);
    if (was_favorite) {
        document_file_controller_.RemoveFavorite(sidebar_item_operation_path_);
        document_file_controller_.AddFavorite(destination);
    }
    active_action_ = "Renamed to " + FileManager::PathToUtf8(destination.filename());
    NotifySuccess(was_favorite ? "Favorite renamed."
        : was_directory ? "Folder renamed." : "File renamed.");
    sidebar_item_operation_path_.clear();
    sidebar_item_operation_error_.clear();
    SetActiveLayer(UiLayer::Main);
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    UpdateFileMenuLabels();
    git_manager_.Invalidate();
    FocusSidebar();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CloseRenameSidebarItem() {
    sidebar_item_operation_path_.clear();
    sidebar_item_operation_error_.clear();
    SetActiveLayer(UiLayer::Main);
    FocusSidebar();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::RequestDeleteSidebarItem(const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        active_action_ = error ? error.message() : "The selected item no longer exists.";
        NotifyWarning("The selected item no longer exists.");
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    sidebar_item_operation_path_ = path;
    sidebar_item_operation_error_.clear();
    delete_sidebar_item_cancel_box_ = {};
    delete_sidebar_item_confirm_box_ = {};
    if (menu_bar_) menu_bar_->CloseDropdown();
    SetActiveLayer(UiLayer::DeleteSidebarItem);
    screen_.PostEvent(ftxui::Event::Custom);
}

ftxui::Element TextltApp::RenderDeleteSidebarItemPopup() {
    if (sidebar_item_operation_path_.empty()) return ftxui::emptyElement();
    std::error_code error;
    const bool is_directory =
        std::filesystem::is_directory(sidebar_item_operation_path_, error);
    const std::string kind = is_directory ? "folder" : "file";
    const std::string name =
        FileManager::PathToUtf8(sidebar_item_operation_path_.filename());
    std::string message = "Permanently delete " + kind + " \"" + name + "\"?";
    if (is_directory) message += " Its contents will also be deleted.";
    if (!sidebar_item_operation_error_.empty())
        message += "\n" + sidebar_item_operation_error_;
    return RenderDeleteConfirmationPopup(
        current_theme_, "Delete " + kind, message,
        delete_sidebar_item_cancel_box_, delete_sidebar_item_confirm_box_);
}

bool TextltApp::HandleDeleteSidebarItemEvent(ftxui::Event event) {
    using namespace ftxui;
    if (event == Event::Custom) return false;
    const bool click = event.is_mouse() &&
        event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed;
    const bool confirm = event == Event::Return ||
        (click && delete_sidebar_item_confirm_box_.Contain(
            event.mouse().x, event.mouse().y));
    const bool cancel = event == Event::Escape ||
        (click && delete_sidebar_item_cancel_box_.Contain(
            event.mouse().x, event.mouse().y));
    if (!confirm && !cancel) return true;

    if (confirm) DeleteSidebarItem();
    else {
        sidebar_item_operation_path_.clear();
        sidebar_item_operation_error_.clear();
        SetActiveLayer(UiLayer::Main);
        FocusSidebar();
        screen_.PostEvent(Event::Custom);
    }
    return true;
}

void TextltApp::DeleteSidebarItem() {
    const std::filesystem::path path = sidebar_item_operation_path_;
    if (path.empty()) return;
    std::error_code type_error;
    const bool was_directory = std::filesystem::is_directory(path, type_error);
    std::string error;
    if (!file_manager_.DeleteItems({path}, true, error)) {
        sidebar_item_operation_error_ = error.empty() ? "Delete failed." : error;
        NotifyError("Could not delete item.");
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }
    document_file_controller_.RemoveFavorite(path);
    sidebar_item_operation_path_.clear();
    sidebar_item_operation_error_.clear();
    SetActiveLayer(UiLayer::Main);
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    UpdateFileMenuLabels();
    git_manager_.Invalidate();
    active_action_ = "Deleted " + FileManager::PathToUtf8(path.filename());
    NotifySuccess(was_directory ? "Folder deleted." : "File deleted.");
    FocusSidebar();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::SaveCurrentFile() {
    const auto document = document_workspace_.ActiveSessionPtr();
    const DocumentSession* session = document_workspace_.ActiveSession();
    if (!document || DocumentWorkspace::IsMemoryOnlySession(session)) {
        OpenFilesModal(FilesModalMode::SaveAs);
        return;
    }

    std::string error;
    if (!document_file_controller_.SaveActiveSession(error)) {
        active_action_ = error.empty() ? "Save failed" : error;
        return;
    }

    RefreshProjectSidebar();
    git_manager_.Invalidate();
    active_action_ = "Saved " + Utf8FromPath(document->path);
}


void TextltApp::SaveAllOpenedFiles() {
    const auto result = document_file_controller_.SaveAllDirtySessions();

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

    if (mode == FilesModalMode::SendConnectFile) {
        success = connect_workspace_->SendFile(path, error);
    } else if (mode == FilesModalMode::Open) {
        success = OpenFile(Utf8FromPath(path), error);
    } else if (mode == FilesModalMode::SaveAs) {
        const auto document = pending_close_session_ ? pending_close_session_
                                                     : document_workspace_.ActiveSessionPtr();
        success = SaveFile(document, path, error);
        if (success && pending_close_session_) {
            CloseOpenSession(std::exchange(pending_close_session_, {}));
        }
    } else if (mode == FilesModalMode::Import) {
        TextImporter importer;
        TextImportResult result = importer.ImportFile(path);
        if (!result.success) {
            error = result.error.empty() ? "Import failed." : result.error;
            active_action_ = error;
            return false;
        }
        success = InsertImportedText(path, result.text, error);
    } else if (mode == FilesModalMode::ImportActions) {
        actions::ActionConfigImportResult result =
            actions::ActionConfigImporter::ImportFile(path);
        if (!result.success) {
            error = result.error.empty() ? "Actions import failed." : result.error;
            active_action_ = error;
            return false;
        }
        std::vector<actions::ActionItem> items;
        items.reserve(result.values.size());
        for (auto& value : result.values) {
            actions::ActionItem item;
            item.title = std::move(value.name);
            item.tabs.push_back({{}, std::move(value.value), false});
            items.push_back(std::move(item));
        }
        auto workspace = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(
            actions_workspace_component_);
        if (!workspace) {
            error = "Actions workspace is not available.";
            return false;
        }
        success = workspace->ImportActions(std::move(items), error);
        if (success) {
            active_action_ = "Imported " + std::to_string(result.values.size()) +
                " actions from " + Utf8FromPath(path.filename());
        }
    } else if (mode == FilesModalMode::ImportNotesMarkdown) {
        auto workspace = std::static_pointer_cast<notes::NotesWorkspaceComponent>(
            notes_workspace_component_);
        if (!workspace) {
            error = "Notes workspace is not available.";
            return false;
        }
        std::vector<notes::NoteBlock> blocks;
        if (!notes::ReadMarkdownNote(path, blocks, error)) return false;
        try {
            success = workspace->ImportNote(
                Utf8FromPath(path.stem()), std::move(blocks), error);
        } catch (const std::exception& exception) {
            error = std::string("Markdown import failed: ") + exception.what();
            return false;
        }
        if (success) active_action_ = "Imported " + Utf8FromPath(path.filename()) + " into Notes";
    } else if (mode == FilesModalMode::OpenFolder) {
        success = OpenWorkspaceFolder(path, error);
    } else if (mode == FilesModalMode::SelectDirectory) {
        success = RegisterProject(path, error);
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
        if (mode != FilesModalMode::SelectDirectory) {
            RefreshProjectSidebar();
        }
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
            directories.push_back(PathFromUtf8(path));
        }
    }
    return directories;
}


bool TextltApp::AddFileModalDirectory(
    const std::filesystem::path& directory,
    std::string& error) {
    const std::string normalized_path =
        EditorConfig::NormalizeDirectoryPath(Utf8FromPath(directory));
    if (normalized_path.empty()) {
        error = "Directory path is empty.";
        return false;
    }

    std::error_code status_error;
    if (!std::filesystem::is_directory(PathFromUtf8(normalized_path), status_error)) {
        error = "Directory does not exist.";
        return false;
    }

    if (editor_config_.IsFileModalDirectory(normalized_path)) {
        error = "Directory already has a shortcut.";
        return false;
    }
    editor_config_.file_modal_directories_.push_back(normalized_path);
    SaveConfig();

    active_action_ = "Added file modal directory " + normalized_path;
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


bool TextltApp::RemoveFileModalDirectory(
    const std::filesystem::path& directory,
    std::string& error) {
    const std::string path = Utf8FromPath(directory);
    if (!editor_config_.IsFileModalDirectory(path)) {
        error = "Current directory is not in the added list.";
        return false;
    }
    if (!editor_config_.RemoveFileModalDirectory(path)) {
        error = "Unable to save the added directory list.";
        return false;
    }

    active_action_ = "Removed file modal directory " + FileManager::PathToUtf8(directory);
    screen_.PostEvent(ftxui::Event::Custom);
    return true;
}


std::filesystem::path TextltApp::CurrentSidebarDirectory() const {
    return std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->CurrentPath();
}


} // namespace textlt
