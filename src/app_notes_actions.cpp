#include "app.hpp"

namespace textlt {

void TextltApp::ShowNotesWorkspace() {
    if (workspace_mode_ == WorkspaceMode::Actions && actions_workspace_component_) {
        std::string error;
        std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_)->Save(error);
        if (!error.empty()) active_action_ = "Actions save failed: " + error;
    }
    if (layout_controller_.IsDistractionModeActive()) {
        SetDistractionEnabled(false);
    }
    CloseDropdown();
    CloseFindPanel();
    workspace_mode_ = WorkspaceMode::Notes;
    workspace_mode_index_ = static_cast<int>(workspace_mode_);
    if (menu_bar_) {
        menu_bar_->SetNotesMenuVisible(true);
        menu_bar_->SetActionsMenuVisible(false);
    }
    sidebar_has_focus_ = false;
    if (notes_workspace_component_) {
        auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
        notes->SetSidebarVisible(editor_config_.show_notes_sidebar);
        notes->SetEditorColumns(editor_config_.notes_editor_columns);
        if (!editor_config_.show_notes_sidebar) {
            notes->FocusMainArea();
        }
        notes->Open();
    }
    active_action_ = "Notes";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShowDocumentsWorkspace() {
    if (notes_workspace_component_) {
        std::string error;
        auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
        if (!notes->Save(error) && !error.empty()) {
            active_action_ = "Notes save failed: " + error;
        }
    }
    if (actions_workspace_component_) {
        std::string error;
        auto actions = std::static_pointer_cast<actions::ActionsWorkspaceComponent>(actions_workspace_component_);
        if (!actions->Save(error) && !error.empty()) active_action_ = "Actions save failed: " + error;
    }
    workspace_mode_ = WorkspaceMode::Documents;
    workspace_mode_index_ = static_cast<int>(workspace_mode_);
    if (menu_bar_) {
        menu_bar_->SetNotesMenuVisible(false);
        menu_bar_->SetActionsMenuVisible(false);
    }
    if (sidebar_panel_) {
        // The Actions workspace temporarily owns the shared Sidebar's list.
        // Restore a document collection before rendering Documents again. This
        // also covers a detour through Notes, where the sidebar is not shown
        // and would otherwise stay in its previous Actions mode.
        auto sidebar = std::static_pointer_cast<SidebarPanel>(sidebar_panel_);
        if (sidebar->IsActionsMode()) sidebar->ShowProject();
    }
    FocusEditor();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ToggleNotesWorkspace() {
    if (workspace_mode_ == WorkspaceMode::Notes) {
        ShowDocumentsWorkspace();
    } else {
        ShowNotesWorkspace();
    }
}

void TextltApp::CommandNotesToggle() { ToggleNotesWorkspace(); }

void TextltApp::CommandNotesNew() {
    if (workspace_mode_ != WorkspaceMode::Notes) {
        ShowNotesWorkspace();
    }
    auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
    if (notes) notes->NewNote();
}

void TextltApp::CommandNotesEditSelected() {
    if (workspace_mode_ != WorkspaceMode::Notes) {
        ShowNotesWorkspace();
    }
    auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
    if (notes) notes->EditSelectedNote();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CommandNotesDeleteSelected() {
    if (workspace_mode_ != WorkspaceMode::Notes) {
        ShowNotesWorkspace();
    }
    auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
    if (notes) notes->DeleteSelection();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CommandNotesHeading1() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ToggleHeading(1);
}
void TextltApp::CommandNotesHeading2() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ToggleHeading(2);
}
void TextltApp::CommandNotesHeading3() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ToggleHeading(3);
}
void TextltApp::CommandNotesParagraph() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->SetBlockType(notes::NoteBlockType::Paragraph);
}
void TextltApp::CommandNotesBulletList() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->SetBlockType(notes::NoteBlockType::BulletItem);
}
void TextltApp::CommandNotesNumberedList() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->SetBlockType(notes::NoteBlockType::NumberedItem);
}
void TextltApp::CommandNotesChecklist() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->SetBlockType(notes::NoteBlockType::CheckItem);
}

} // namespace textlt
