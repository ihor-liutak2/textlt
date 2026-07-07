#include "app.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

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
    document_workspace_.ClampActiveEditorPaneIndex(editor_pane_components_.size());
    if (!editor_pane_components_.empty()) {
        text_editor_ = editor_pane_components_[document_workspace_.ActiveEditorPaneIndex()];
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
    const auto document = document_workspace_.ActiveDocument();
    if (document) {
        return document;
    }
    const auto editor = ActiveEditor();
    return editor ? editor->GetDocument() : nullptr;
}


void TextltApp::AddOpenDocument(std::shared_ptr<Document> doc) {
    if (!doc) {
        return;
    }

    document_file_controller_.AddDocument(std::move(doc));
    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
}


void TextltApp::EnsureOneOpenDocument() {
    document_file_controller_.EnsureOneDocument(VisibleEditorPaneCount());
    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
}


void TextltApp::RemoveOpenDocument(size_t index) {
    document_file_controller_.RemoveDocument(index, VisibleEditorPaneCount());
    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
}


void TextltApp::CloseCurrentFile() {
    std::string closed_name;
    document_file_controller_.CloseActiveDocument(VisibleEditorPaneCount(), closed_name);
    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    active_action_ = "Closed " + (closed_name.empty() ? "file" : closed_name);
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::CloseAllOpenedFiles() {
    document_file_controller_.CloseAllDocuments(VisibleEditorPaneCount());
    BindEditorComponentsToWorkspace();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    active_action_ = "Closed all files";
    CloseDropdown();
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::PersistOpenedDocuments() {
    document_file_controller_.PersistOpenedDocuments();
}


void TextltApp::RestoreOpenedDocuments() {
    if (document_file_controller_.RestoreOpenedDocuments(VisibleEditorPaneCount())) {
        BindEditorComponentsToWorkspace();
        RefreshOpenedDocumentsSidebar();
    }
}


void TextltApp::ActivateOpenDocument(size_t index) {
    if (!document_file_controller_.ActivateDocument(index, VisibleEditorPaneCount())) {
        return;
    }

    BindEditorComponentsToWorkspace();
    const auto editor_ptr = ActiveEditor();
    RefreshOpenedDocumentsSidebar();
    UpdateFileMenuLabels();
    active_action_ = "Switched to " + (editor_ptr ? editor_ptr->CurrentFilePath() : "file");
    screen_.PostEvent(ftxui::Event::Custom);
}


void TextltApp::RefreshOpenedDocumentsSidebar() {
    auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
    std::vector<SidebarPanel::OpenedFileEntry> entries;
    entries.reserve(document_workspace_.OpenDocuments().size());

    for (size_t index = 0; index < document_workspace_.OpenDocuments().size(); ++index) {
        const auto& doc = document_workspace_.OpenDocuments()[index];
        if (!doc) continue;

        std::string label = doc->path.filename().string();
        if (label.empty()) {
            label = doc->path.string();
        }
        entries.push_back({doc->path, label, doc->is_dirty, index == document_workspace_.ActiveDocumentIndex()});
    }

    sidebar->SetOpenedFiles(std::move(entries), document_workspace_.ActiveDocumentIndex());
}


std::string TextltApp::ActiveDocumentFavoritePath() const {
    return document_file_controller_.ActiveDocumentFavoritePath();
}


void TextltApp::PersistActiveFavoriteCursor() {
    document_file_controller_.PersistActiveFavoriteCursor();
}


void TextltApp::RestoreFavoriteCursor(const std::string& path) {
    document_file_controller_.RestoreFavoriteCursor(path);
}


void TextltApp::ToggleActiveFavorite() {
    const auto result = document_file_controller_.ToggleActiveFavorite();
    if (result.status == DocumentFileController::FavoriteToggleStatus::NeedsSave) {
        active_action_ = "Save the file before adding it to favorites";
        CloseDropdown();
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    }

    if (result.status == DocumentFileController::FavoriteToggleStatus::Removed) {
        active_action_ = "Removed favorite " + result.path;
    } else {
        active_action_ = "Added favorite " + result.path;
    }

    UpdateFileMenuLabels();
    std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->Refresh();
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}


} // namespace textlt
