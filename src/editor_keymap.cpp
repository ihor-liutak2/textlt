#include "editor_keymap.hpp"

#include <string>

#include "app.hpp"
#include "editor_component.hpp"
#include "ftxui/component/event.hpp"

namespace textlt {

std::shared_ptr<EditorComponent> EditorKeymap::ActiveEditorPtr(TextltApp& app) {
    return std::static_pointer_cast<EditorComponent>(app.text_editor_);
}

bool EditorKeymap::EnsureWritable(TextltApp& app, const std::shared_ptr<EditorComponent>& editor) {
    if (!editor) {
        return false;
    }
    if (editor->IsReadOnly()) {
        app.active_action_ = "Document is read-only";
        app.screen_.PostEvent(ftxui::Event::Custom);
        return false;
    }
    return true;
}

void EditorKeymap::FinishEditorAction(TextltApp& app) {
    app.screen_.PostEvent(ftxui::Event::Custom);
}

void EditorKeymap::MoveCursor(EditorComponent& editor, MoveAction action, bool extend_selection) {
    editor.EndTypingGroup();
    const bool preserve_selection = extend_selection || editor.SelectionAnchorModeActive();
    if (preserve_selection) {
        editor.BeginSelection();
    }

    switch (action) {
        case MoveAction::Left: editor.MoveCursorLeft(); break;
        case MoveAction::Right: editor.MoveCursorRight(); break;
        case MoveAction::Up: editor.MoveCursorUp(); break;
        case MoveAction::Down: editor.MoveCursorDown(); break;
        case MoveAction::WordLeft: editor.MoveCursorToPreviousWord(); break;
        case MoveAction::WordRight: editor.MoveCursorToNextWord(); break;
        case MoveAction::Home: editor.MoveCursorHome(); break;
        case MoveAction::End: editor.MoveCursorEnd(); break;
        case MoveAction::DocumentStart: editor.MoveCursorDocumentStart(); break;
        case MoveAction::DocumentEnd: editor.MoveCursorDocumentEnd(); break;
        case MoveAction::PageUp: editor.MoveCursorPageUp(); break;
        case MoveAction::PageDown: editor.MoveCursorPageDown(); break;
        case MoveAction::ParagraphUp: editor.MoveCursorToPreviousParagraph(); break;
        case MoveAction::ParagraphDown: editor.MoveCursorToNextParagraph(); break;
        case MoveAction::ParagraphSelectionUp:
            editor.MoveCursorToPreviousParagraphSelectionBoundary();
            break;
        case MoveAction::ParagraphSelectionDown:
            editor.MoveCursorToNextParagraphSelectionBoundary();
            break;
    }

    editor.ClampCursorToBuffer();
    if (!preserve_selection) {
        editor.ClearSelection();
    }
    editor.UpdateScroll();
}

std::vector<ShortcutBindingDefinition> EditorKeymap::DefaultBindings() {
    return {
        {ShortcutContext::Text, "editor.word_left", "Move to previous word", "Navigation", "Ctrl+Left"},
        {ShortcutContext::Text, "editor.word_right", "Move to next word", "Navigation", "Ctrl+Right"},
        {ShortcutContext::Text, "editor.document_start", "Move to document start", "Navigation", "Ctrl+Home"},
        {ShortcutContext::Text, "editor.document_end", "Move to document end", "Navigation", "Ctrl+End"},
        {ShortcutContext::Text, "editor.paragraph_up", "Move to previous paragraph", "Navigation", "Ctrl+Up"},
        {ShortcutContext::Text, "editor.paragraph_down", "Move to next paragraph", "Navigation", "Ctrl+Down"},
        {ShortcutContext::Text, "editor.select_left", "Select left", "Selection", "Shift+Left"},
        {ShortcutContext::Text, "editor.select_right", "Select right", "Selection", "Shift+Right"},
        {ShortcutContext::Text, "editor.select_up", "Select up", "Selection", "Shift+Up"},
        {ShortcutContext::Text, "editor.select_down", "Select down", "Selection", "Shift+Down"},
        {ShortcutContext::Text, "editor.select_line_start", "Select to line start", "Selection", "Shift+Home"},
        {ShortcutContext::Text, "editor.select_line_end", "Select to line end", "Selection", "Shift+End"},
        {ShortcutContext::Text, "editor.select_page_up", "Select one page up", "Selection", "Shift+PageUp"},
        {ShortcutContext::Text, "editor.select_page_down", "Select one page down", "Selection", "Shift+PageDown"},
        {ShortcutContext::Text, "editor.select_document_start", "Select to document start", "Selection", "Ctrl+Shift+Home"},
        {ShortcutContext::Text, "editor.select_document_end", "Select to document end", "Selection", "Ctrl+Shift+End"},
        {ShortcutContext::Text, "editor.select_word_left", "Select previous word", "Selection", "Ctrl+Shift+Left"},
        {ShortcutContext::Text, "editor.select_word_right", "Select next word", "Selection", "Ctrl+Shift+Right"},
        {ShortcutContext::Text, "editor.select_paragraph_up", "Select previous paragraph boundary", "Selection", "Ctrl+Shift+Up"},
        {ShortcutContext::Text, "editor.select_paragraph_down", "Select next paragraph boundary", "Selection", "Ctrl+Shift+Down"},
        {ShortcutContext::Text, "editor.select_current_line", "Select current line", "Selection", ""},
        {ShortcutContext::Text, "editor.toggle_selection_anchor", "Toggle selection anchor", "Selection", ""},
        {ShortcutContext::Text, "editor.clear_selection", "Clear selection", "Selection", "Escape"},
        {ShortcutContext::Text, "editor.delete_word_backward", "Delete previous word", "Deletion", "Ctrl+Backspace"},
        {ShortcutContext::Text, "editor.delete_word_forward", "Delete next word", "Deletion", "Ctrl+Delete"},
        {ShortcutContext::Text, "editor.move_line_up", "Move line up", "Lines", "Alt+Up"},
        {ShortcutContext::Text, "editor.move_line_down", "Move line down", "Lines", "Alt+Down"},
        {ShortcutContext::Text, "editor.duplicate_line_down", "Duplicate line down", "Lines", "Alt+Shift+Down"},
        {ShortcutContext::Text, "editor.outdent_lines", "Outdent selected lines", "Lines", "Shift+Tab"},
    };
}

bool EditorKeymap::RunAction(TextltApp& app, const std::string& action_id) const {
    const std::shared_ptr<EditorComponent> editor = ActiveEditorPtr(app);
    if (!editor) {
        return false;
    }
    editor->BindViewportCursorState();

    if (action_id == "editor.word_left") {
        MoveCursor(*editor, MoveAction::WordLeft, false);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.word_right") {
        MoveCursor(*editor, MoveAction::WordRight, false);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.document_start") {
        MoveCursor(*editor, MoveAction::DocumentStart, false);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.document_end") {
        MoveCursor(*editor, MoveAction::DocumentEnd, false);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.paragraph_up") {
        MoveCursor(*editor, MoveAction::ParagraphUp, false);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.paragraph_down") {
        MoveCursor(*editor, MoveAction::ParagraphDown, false);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_left") {
        MoveCursor(*editor, MoveAction::Left, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_right") {
        MoveCursor(*editor, MoveAction::Right, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_up") {
        MoveCursor(*editor, MoveAction::Up, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_down") {
        MoveCursor(*editor, MoveAction::Down, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_line_start") {
        MoveCursor(*editor, MoveAction::Home, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_line_end") {
        MoveCursor(*editor, MoveAction::End, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_page_up") {
        MoveCursor(*editor, MoveAction::PageUp, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_page_down") {
        MoveCursor(*editor, MoveAction::PageDown, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_document_start") {
        MoveCursor(*editor, MoveAction::DocumentStart, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_document_end") {
        MoveCursor(*editor, MoveAction::DocumentEnd, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_word_left") {
        MoveCursor(*editor, MoveAction::WordLeft, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_word_right") {
        MoveCursor(*editor, MoveAction::WordRight, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_paragraph_up") {
        MoveCursor(*editor, MoveAction::ParagraphSelectionUp, true);
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.select_paragraph_down") {
        MoveCursor(*editor, MoveAction::ParagraphSelectionDown, true);
        FinishEditorAction(app);
        return true;
    }

    if (action_id == "editor.select_current_line") {
        editor->SelectCurrentLine();
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.toggle_selection_anchor") {
        editor->ToggleSelectionAnchor();
        FinishEditorAction(app);
        return true;
    }
    if (action_id == "editor.clear_selection") {
        if (editor->HasSelection() || editor->SelectionAnchorModeActive()) {
            editor->ClearSelection();
            FinishEditorAction(app);
        }
        return true;
    }

    if (action_id == "editor.delete_word_backward") {
        if (EnsureWritable(app, editor)) {
            editor->DeleteWordBackward();
            FinishEditorAction(app);
        }
        return true;
    }
    if (action_id == "editor.delete_word_forward") {
        if (EnsureWritable(app, editor)) {
            editor->DeleteWordForward();
            FinishEditorAction(app);
        }
        return true;
    }
    if (action_id == "editor.move_line_up") {
        if (EnsureWritable(app, editor) && editor->MoveLinesUp()) {
            FinishEditorAction(app);
        }
        return true;
    }
    if (action_id == "editor.move_line_down") {
        if (EnsureWritable(app, editor) && editor->MoveLinesDown()) {
            FinishEditorAction(app);
        }
        return true;
    }
    if (action_id == "editor.duplicate_line_down") {
        if (EnsureWritable(app, editor) && editor->DuplicateLines()) {
            FinishEditorAction(app);
        }
        return true;
    }
    if (action_id == "editor.outdent_lines") {
        if (EnsureWritable(app, editor)) {
            editor->OutdentLines();
            FinishEditorAction(app);
        }
        return true;
    }

    return false;
}

} // namespace textlt
