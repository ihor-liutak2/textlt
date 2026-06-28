#include "app.hpp"

#include <filesystem>
#include <memory>
#include <string>

#include "document.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {


void TextltApp::SwitchEditorFocus() {
    if (!editor_config_.show_file_explorer) {
        FocusEditor();
        return;
    }
    if (sidebar_has_focus_) {
        FocusEditor();
    } else {
        FocusSidebar();
    }
}


void TextltApp::FocusEditor() {
    sidebar_has_focus_ = false;
    SetActiveLayer(UiLayer::Main);
    if (active_editor_pane_index_ >= editor_pane_components_.size()) {
        active_editor_pane_index_ = 0;
    }
    if (!editor_pane_components_.empty()) {
        text_editor_ = editor_pane_components_[active_editor_pane_index_];
    }
    if (text_editor_) {
        text_editor_->TakeFocus();
    }
}


void TextltApp::FocusSidebar() {
    sidebar_has_focus_ = true;
    SetActiveLayer(UiLayer::Main);
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->FocusMenu();
}


void TextltApp::ToggleFileExplorer() {
    editor_config_.show_file_explorer = !editor_config_.show_file_explorer;
    if (editor_config_.show_file_explorer) {
        active_action_ = "File Explorer enabled";
        FocusSidebar();
    } else {
        active_action_ = "File Explorer disabled";
        FocusEditor();
    }
    SaveConfig();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::HandleCtrlBFileExplorer() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        sidebar->ShowProject();
        active_action_ = "File Explorer enabled";
        FocusSidebar();
        SaveConfig();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (!sidebar_has_focus_) {
        sidebar->ShowProject();
        active_action_ = "Project files";
        FocusSidebar();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    editor_config_.show_file_explorer = false;
    active_action_ = "File Explorer disabled";
    FocusEditor();
    SaveConfig();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::ShowOpenedFilesSidebar() {
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        SaveConfig();
    }

    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ShowOpenedFiles();
    FocusSidebar();
    active_action_ = "Opened files";
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::ToggleSidebarOpenedProject() {
    if (!editor_config_.show_file_explorer) {
        editor_config_.show_file_explorer = true;
        SaveConfig();
    }

    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ToggleOpenedProject();
    FocusSidebar();
    active_action_ = "Switched File Explorer tab";
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::RefreshProjectSidebar() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    sidebar->ShowProject();
    sidebar->Refresh();
}


std::shared_ptr<Document> TextltApp::ActiveDocument() const {
    if (active_document_index_ < open_documents_.size()) {
        return open_documents_[active_document_index_];
    }
    const auto editor = ActiveEditor();
    return editor ? editor->GetDocument() : nullptr;
}


int TextltApp::FindOpenDocument(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(path, error);
    if (error) {
        normalized = path;
    }

    for (size_t index = 0; index < open_documents_.size(); ++index) {
        const auto& doc = open_documents_[index];
        if (!doc) continue;

        std::filesystem::path doc_path = std::filesystem::absolute(doc->path, error);
        if (error) {
            doc_path = doc->path;
            error.clear();
        }
        if (doc_path == normalized) {
            return static_cast<int>(index);
        }
    }
    return -1;
}


void TextltApp::AddOpenDocument(std::shared_ptr<Document> doc) {
    if (!doc) {
        return;
    }

    open_documents_.push_back(doc);
    active_document_index_ = open_documents_.size() - 1;
    AssignDocumentToActivePane(active_document_index_);
    RefreshOpenedDocumentsSidebar();
}


bool TextltApp::IsMemoryOnlyDocument(const std::shared_ptr<Document>& doc) const {
    if (!doc) {
        return false;
    }
    const std::string path = doc->path.string();
    return path.empty() || path == "Untitled" || path == "untitled.txt";
}


void TextltApp::EnsureOneOpenDocument() {
    if (!open_documents_.empty()) {
        if (active_document_index_ >= open_documents_.size()) {
            active_document_index_ = open_documents_.size() - 1;
        }
        AssignDocumentToActivePane(active_document_index_);
        SyncEditorPaneDocuments();
        RefreshOpenedDocumentsSidebar();
        return;
    }

    auto doc = std::make_shared<Document>();
    doc->Reset();
    AddOpenDocument(doc);
}


void TextltApp::RemoveOpenDocument(size_t index) {
    if (index >= open_documents_.size()) {
        return;
    }

    open_documents_.erase(open_documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (open_documents_.empty()) {
        active_document_index_ = 0;
        EnsureOneOpenDocument();
        return;
    }

    if (active_document_index_ >= open_documents_.size()) {
        active_document_index_ = open_documents_.size() - 1;
    } else if (index < active_document_index_) {
        --active_document_index_;
    }

    for (EditorPaneState& pane : editor_panes_) {
        if (pane.document_index > index) {
            --pane.document_index;
        } else if (pane.document_index == index) {
            pane.document_index = active_document_index_;
        }
    }

    SyncEditorPaneDocuments();
    RefreshOpenedDocumentsSidebar();
}


void TextltApp::CloseCurrentFile() {
    if (open_documents_.empty()) {
        EnsureOneOpenDocument();
        return;
    }

    const std::string closed_name =
        open_documents_[active_document_index_]
            ? open_documents_[active_document_index_]->path.filename().string()
            : "";
    RemoveOpenDocument(active_document_index_);
    PersistOpenedDocuments();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    active_action_ = "Closed " + (closed_name.empty() ? "file" : closed_name);
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseAllOpenedFiles() {
    open_documents_.clear();
    active_document_index_ = 0;
    EnsureOneOpenDocument();
    PersistOpenedDocuments();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    active_action_ = "Closed all files";
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::PersistOpenedDocuments() {
    OpenedConfig opened_config;
    bool active_index_saved = false;

    for (size_t doc_index = 0; doc_index < open_documents_.size(); ++doc_index) {
        const auto& doc = open_documents_[doc_index];
        if (!doc) {
            continue;
        }

        OpenedFileState entry;
        entry.row = doc->cursor_row;
        entry.column = doc->cursor_col;

        if (IsMemoryOnlyDocument(doc)) {
            const std::string content = doc->ToContent();
            if (content.empty()) {
                continue;
            }
            entry.memory_only = true;
            entry.path = "Untitled";
            entry.content = content;
            if (doc_index == active_document_index_) {
                opened_config.active_index = opened_config.files.size();
                active_index_saved = true;
            }
            opened_config.files.push_back(std::move(entry));
            continue;
        }

        const std::string normalized = EditorConfig::NormalizeFavoritePath(doc->path.string());
        if (normalized.empty()) {
            continue;
        }
        std::error_code error_code;
        if (!std::filesystem::is_regular_file(normalized, error_code)) {
            continue;
        }
        entry.memory_only = false;
        entry.path = normalized;
        if (doc_index == active_document_index_) {
            opened_config.active_index = opened_config.files.size();
            active_index_saved = true;
        }
        opened_config.files.push_back(std::move(entry));
    }

    if (!active_index_saved || opened_config.active_index >= opened_config.files.size()) {
        opened_config.active_index = opened_config.files.empty() ? 0 : opened_config.files.size() - 1;
    }
    opened_config_store_.Save(opened_config);
}


void TextltApp::OpenRestoredDocument(const OpenedFileState& entry) {
    if (entry.memory_only) {
        auto doc = std::make_shared<Document>();
        doc->Reset();
        doc->LoadContent(entry.content, "Untitled");
        doc->is_dirty = true;
        doc->SetCursorPosition(entry.row, entry.column);
        AddOpenDocument(doc);
        return;
    }

    std::error_code error_code;
    if (!std::filesystem::is_regular_file(entry.path, error_code)) {
        return;
    }

    std::string error;
    auto doc = file_manager_.Open(entry.path, error);
    if (!doc) {
        return;
    }
    doc->SetCursorPosition(entry.row, entry.column);
    AddOpenDocument(doc);
}


void TextltApp::RestoreOpenedDocuments() {
    const OpenedConfig opened_config = opened_config_store_.Load();
    if (opened_config.files.empty()) {
        return;
    }

    open_documents_.clear();
    active_document_index_ = 0;
    for (const OpenedFileState& entry : opened_config.files) {
        OpenRestoredDocument(entry);
    }

    if (!open_documents_.empty()) {
        active_document_index_ = std::min(opened_config.active_index, open_documents_.size() - 1);
        AssignDocumentToActivePane(active_document_index_);
        SyncEditorPaneDocuments();
        RefreshOpenedDocumentsSidebar();
    }

    PersistOpenedDocuments();
}


void TextltApp::ActivateOpenDocument(size_t index) {
    if (index >= open_documents_.size() || !open_documents_[index]) {
        return;
    }

    PersistActiveFavoriteCursor();
    AssignDocumentToActivePane(index);
    auto editor_ptr = ActiveEditor();
    RestoreFavoriteCursor(open_documents_[index]->path.string());
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    PersistOpenedDocuments();
    active_action_ = "Switched to " + (editor_ptr ? editor_ptr->CurrentFilePath() : open_documents_[index]->path.string());
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::RefreshOpenedDocumentsSidebar() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    std::vector<SidebarPanel::OpenedFileEntry> entries;
    entries.reserve(open_documents_.size());

    for (size_t index = 0; index < open_documents_.size(); ++index) {
        const auto& doc = open_documents_[index];
        if (!doc) continue;

        std::string label = doc->path.filename().string();
        if (label.empty()) {
            label = doc->path.string();
        }
        entries.push_back({doc->path, label, doc->is_dirty, index == active_document_index_});
    }

    sidebar->SetOpenedFiles(std::move(entries), active_document_index_);
}


std::string TextltApp::ActiveDocumentFavoritePath() const {
    const auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    return EditorConfig::NormalizeFavoritePath(editor_ptr->CurrentFilePath());
}


void TextltApp::PersistActiveFavoriteCursor() {
    auto editor_ptr = std::static_pointer_cast<EditorComponent>(text_editor_);
    const std::string favorite_path =
        EditorConfig::NormalizeFavoritePath(editor_ptr->CurrentFilePath());
    if (favorite_path.empty() || !editor_config_.IsFavorite(favorite_path)) {
        return;
    }

    const size_t row = editor_ptr->GetCursorRow();
    const size_t column = editor_ptr->GetCursorCol();
    editor_config_.UpdateFavoriteCursor(favorite_path, row, column);
}


void TextltApp::RestoreFavoriteCursor(const std::string& path) {
    const FavoriteEntry* favorite = editor_config_.FindFavorite(path);
    if (!favorite) {
        return;
    }

    std::static_pointer_cast<EditorComponent>(text_editor_)
        ->SetCursorPosition(favorite->row, favorite->column);
}


void TextltApp::ToggleActiveFavorite() {
    const std::string favorite_path = ActiveDocumentFavoritePath();
    if (favorite_path.empty()) {
        active_action_ = "Save the file before adding it to favorites";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (editor_config_.IsFavorite(favorite_path)) {
        editor_config_.RemoveFavorite(favorite_path);
        active_action_ = "Removed favorite " + favorite_path;
    } else {
        std::error_code error;
        if (!std::filesystem::is_regular_file(favorite_path, error)) {
            active_action_ = "Save the file before adding it to favorites";
            CloseDropdown();
            screen_.PostEvent(ftxui::Event::Custom);
            return;
        }
        editor_config_.AddFavorite(favorite_path);
        PersistActiveFavoriteCursor();
        active_action_ = "Added favorite " + favorite_path;
    }

    UpdateFileMenuLabels();
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


} // namespace textlt
