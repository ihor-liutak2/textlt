#include "editor/document_session.hpp"

namespace textlt {

HistoryManager::State DocumentSession::CurrentState() const {
    return {
        lines.empty() ? std::vector<std::string>{""} : lines,
        static_cast<int>(CursorCol()),
        static_cast<int>(CursorRow()),
    };
}

void DocumentSession::ApplyState(const HistoryManager::State& state) {
    buffer.SetLines(state.lines);
    CursorCol() = state.cursor_x < 0 ? 0 : static_cast<size_t>(state.cursor_x);
    CursorRow() = state.cursor_y < 0 ? 0 : static_cast<size_t>(state.cursor_y);
    EnsureValidBuffer();
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
