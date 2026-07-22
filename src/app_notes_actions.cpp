#include "app.hpp"

namespace textlt {

void TextltApp::ShowNotesWorkspace() {
    if (layout_controller_.IsDistractionModeActive()) {
        SetDistractionEnabled(false);
    }
    CloseDropdown();
    CloseFindPanel();
    workspace_mode_ = WorkspaceMode::Notes;
    workspace_mode_index_ = static_cast<int>(workspace_mode_);
    sidebar_has_focus_ = false;
    if (notes_workspace_component_) {
        auto notes = std::static_pointer_cast<notes::NotesWorkspaceComponent>(notes_workspace_component_);
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
    workspace_mode_ = WorkspaceMode::Documents;
    workspace_mode_index_ = static_cast<int>(workspace_mode_);
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
