#include "editor/document_session.hpp"

#include <algorithm>

namespace textlt {

HistoryManager::State DocumentSession::CurrentState() const {
    return {
        lines.empty() ? std::vector<std::string>{""} : lines,
        static_cast<int>(CursorCol()),
        static_cast<int>(CursorRow()),
        SelectionState().active,
        static_cast<int>(SelectionState().anchor_x),
        static_cast<int>(SelectionState().anchor_y),
    };
}

void DocumentSession::ApplyState(const HistoryManager::State& state) {
    buffer.SetLines(state.lines);
    CursorCol() = state.cursor_x < 0 ? 0 : static_cast<size_t>(state.cursor_x);
    CursorRow() = state.cursor_y < 0 ? 0 : static_cast<size_t>(state.cursor_y);
    EnsureValidBuffer();
    ClampCursor();
    CursorState().selection_anchor_mode = false;

    if (state.selection_active) {
        SelectionState().active = true;
        SelectionState().anchor_y = state.selection_anchor_y < 0
            ? 0
            : static_cast<size_t>(state.selection_anchor_y);
        SelectionState().anchor_y = std::min(SelectionState().anchor_y, lines.size() - 1);
        SelectionState().anchor_x = state.selection_anchor_x < 0
            ? 0
            : static_cast<size_t>(state.selection_anchor_x);
        SelectionState().anchor_x = std::min(
            SelectionState().anchor_x,
            lines[SelectionState().anchor_y].size());
    } else {
        ClearSelection();
    }
}

void DocumentSession::SaveSnapshot() {
    ClampCursor();
    history.PushSnapshot(CurrentState());
}

void DocumentSession::SaveSnapshotForTyping(const std::string& input, bool has_selection) {
    ClampCursor();
    history.PushSnapshotForTyping(input, CurrentState(), has_selection);
}

void DocumentSession::EndTypingGroup() {
    history.EndTypingGroup();
}

bool DocumentSession::Undo() {
    HistoryManager::State state;
    if (!history.Undo(CurrentState(), &state)) {
        return false;
    }
    ApplyState(state);
    return true;
}

bool DocumentSession::Redo() {
    HistoryManager::State state;
    if (!history.Redo(CurrentState(), &state)) {
        return false;
    }
    ApplyState(state);
    return true;
}

} // namespace textlt
