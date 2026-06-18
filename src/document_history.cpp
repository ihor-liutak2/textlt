#include "document.hpp"

namespace textlt {

HistoryManager::State Document::CurrentState() const {
    return {
        lines.empty() ? std::vector<std::string>{""} : lines,
        static_cast<int>(cursor_col),
        static_cast<int>(cursor_row),
    };
}

void Document::ApplyState(const HistoryManager::State& state) {
    lines = state.lines;
    cursor_col = state.cursor_x < 0 ? 0 : static_cast<size_t>(state.cursor_x);
    cursor_row = state.cursor_y < 0 ? 0 : static_cast<size_t>(state.cursor_y);
    EnsureValidBuffer();
}

void Document::SaveSnapshot() {
    ClampCursor();
    history.PushSnapshot(CurrentState());
}

void Document::SaveSnapshotForTyping(const std::string& input, bool has_selection) {
    ClampCursor();
    history.PushSnapshotForTyping(input, CurrentState(), has_selection);
}

void Document::EndTypingGroup() {
    history.EndTypingGroup();
}

bool Document::Undo() {
    HistoryManager::State state;
    if (!history.Undo(CurrentState(), &state)) {
        return false;
    }
    ApplyState(state);
    return true;
}

bool Document::Redo() {
    HistoryManager::State state;
    if (!history.Redo(CurrentState(), &state)) {
        return false;
    }
    ApplyState(state);
    return true;
}

} // namespace textlt
