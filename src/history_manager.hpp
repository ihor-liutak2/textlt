#pragma once

#include <string>
#include <vector>

namespace textlt {

class HistoryManager {
public:
    struct State {
        std::vector<std::string> lines;
        int cursor_x = 0;
        int cursor_y = 0;
        bool selection_active = false;
        int selection_anchor_x = 0;
        int selection_anchor_y = 0;
    };

    void Clear();
    void EndTypingGroup();
    void PushSnapshot(State state);
    void PushSnapshotForTyping(const std::string& input, State state, bool has_selection);
    bool Undo(State current_state, State* restored_state);
    bool Redo(State current_state, State* restored_state);

private:
    static constexpr size_t kMaxHistory = 100;

    std::vector<State> undo_stack_;
    std::vector<State> redo_stack_;
    bool typing_group_active_ = false;
};

} // namespace textlt
