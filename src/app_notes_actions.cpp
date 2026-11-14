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
        notes->Open();
    }
    active_action_ = "Notes";
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::ShowDocumentsWorkspace() {
    const bool returning_from_actions = workspace_mode_ == WorkspaceMode::Actions;
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
    if (returning_from_actions && sidebar_panel_) {
        // The Actions workspace temporarily owns the shared Sidebar's list.
        // Restore a document collection before rendering Documents again.
        editor_config_.show_file_explorer = true;
        std::static_pointer_cast<SidebarPanel>(sidebar_panel_)->ShowProject();
        SaveConfig();
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

void TextltApp::CommandNotesBold() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ToggleMark(notes::NoteMark::Bold);
}
void TextltApp::CommandNotesItalic() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ToggleMark(notes::NoteMark::Italic);
}
void TextltApp::CommandNotesUnderline() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ToggleMark(notes::NoteMark::Underlined);
}
void TextltApp::CommandNotesStrikethrough() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ToggleMark(notes::NoteMark::Strikethrough);
}
void TextltApp::CommandNotesClearFormatting() {
    if (workspace_mode_ == WorkspaceMode::Notes) std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_)->ClearFormatting();
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
