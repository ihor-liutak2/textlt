#include "app.hpp"

#include <utility>

#include "path_utils.hpp"

namespace textlt {
namespace {

bool CanFindSelection(const DocumentSession& document) {
    return document.HasSelection() && document.CursorRow() == document.SelectionState().anchor_y;
}

bool HasFilePath(const DocumentSession& document) {
    return !document.IsMemoryOnly() && !document.temporary;
}

} // namespace

bool TextltApp::OpenEditorContextMenu(int x, int y) {
    if (!MainViewCanActivateEditorPane()) return false;

    const size_t pane_count = VisibleEditorPaneCount();
    size_t pane = 0;
    for (; pane < pane_count; ++pane) {
        const auto* viewport = document_workspace_.PaneViewport(pane);
        if (viewport && viewport->Box().Contain(x, y)) break;
    }
    if (pane == pane_count) return false;

    if (pane != document_workspace_.ActiveEditorPaneIndex()) SetActiveEditorPane(pane);
    else FocusEditor();
    if (find_panel_focus_) {
        find_panel_focus_ = false;
        find_replace_panel_->SetFocused(false);
    }

    const auto editor = ActiveEditor();
    const auto document = editor ? editor->GetSession() : nullptr;
    if (!document) return false;
    const bool selection = document->HasSelection();
    const bool editable = !document->read_only;
    const bool has_text = document->lines.size() > 1 ||
        (!document->lines.empty() && !document->lines.front().empty());
    const auto item = [this](const char* id, bool enabled) {
        const auto* command = command_registry_.Find(id);
        return ContextMenuComponent::Item{id, command ? command->title : id,
            enabled && command_registry_.Enabled(id)};
    };
    editor_context_menu_.Open(x, y, {
        item("edit.undo", editable && document->history.CanUndo()),
        item("edit.redo", editable && document->history.CanRedo()),
        {},
        item("edit.cut", editable && selection),
        item("edit.copy", selection),
        item("edit.paste", editable),
        item("edit.select_all", has_text),
        {},
        item("edit.toggle_case", editable && selection),
        item("edit.uppercase", editable && selection),
        item("edit.lowercase", editable && selection),
        {},
        item("edit.indent_right", editable),
        item("edit.indent_left", document->CanOutdentLines()),
        {},
        item("edit.find_selection", CanFindSelection(*document)),
        item("file.copy_path", HasFilePath(*document)),
    });
    return true;
}

void TextltApp::CommandEditChangeCase(transform::CaseMode mode) {
    const auto editor = ActiveEditor();
    if (!editor || editor->IsReadOnly()) return;
    FocusEditor();
    if (editor->ChangeCase(mode)) {
        switch (mode) {
            case transform::CaseMode::Toggle: active_action_ = "Toggled case"; break;
            case transform::CaseMode::Upper: active_action_ = "Converted to uppercase"; break;
            case transform::CaseMode::Lower: active_action_ = "Converted to lowercase"; break;
        }
    }
    CloseDropdown();
    screen_.PostEvent(ftxui::Event::Custom);
}

void TextltApp::CommandEditorIndentRight() {
    const auto editor = ActiveEditor();
    if (!editor || editor->IsReadOnly()) return;
    FocusEditor();
    if (editor->IndentLines()) active_action_ = "Indented lines";
}

void TextltApp::CommandFindSelection() {
    const auto editor = ActiveEditor();
    const auto document = editor ? editor->GetSession() : nullptr;
    if (document && CanFindSelection(*document)) {
        OpenFindPanel(false, editor->GetSelectedText());
    }
}

void TextltApp::CommandCopyFilePath() {
    const auto document = ActiveSessionPtr();
    if (!document || !HasFilePath(*document)) {
        NotifyWarning("No file path to copy.");
        return;
    }

    std::error_code error;
    const auto path = std::filesystem::absolute(document->path, error);
    active_action_ = !error && clipboard_controller_.WriteText(Utf8FromPath(path))
        ? "Copied file path to clipboard" : "Could not copy file path to the system clipboard";
    if (active_action_ == "Copied file path to clipboard") NotifySuccess("Path copied.");
    else NotifyError("Could not copy path.");
}

} // namespace textlt
