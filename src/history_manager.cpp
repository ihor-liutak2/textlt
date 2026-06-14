#include "history_manager.hpp"

#include <cctype>
#include <utility>

namespace textlt {

void HistoryManager::Clear() {
    undo_stack_.clear();
    redo_stack_.clear();
    typing_group_active_ = false;
}

void HistoryManager::EndTypingGroup() {
    typing_group_active_ = false;
}

void HistoryManager::PushSnapshot(State state) {
    if (!undo_stack_.empty() &&
        undo_stack_.back().lines == state.lines &&
        undo_stack_.back().cursor_x == state.cursor_x &&
        undo_stack_.back().cursor_y == state.cursor_y) {
        redo_stack_.clear();
        return;
    }

    undo_stack_.push_back(std::move(state));
    if (undo_stack_.size() > kMaxHistory) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
}

void HistoryManager::PushSnapshotForTyping(const std::string& input,
                                           State state,
                                           bool has_selection) {
    const bool boundary =
        input.size() == 1 &&
        std::isspace(static_cast<unsigned char>(input.front()));

    if (!typing_group_active_ || boundary || has_selection) {
        PushSnapshot(std::move(state));
    }

    typing_group_active_ = !boundary;
}

bool HistoryManager::Undo(State current_state, State* restored_state) {
    EndTypingGroup();
    if (undo_stack_.empty() || restored_state == nullptr) {
        return false;
    }

    redo_stack_.push_back(std::move(current_state));
    if (redo_stack_.size() > kMaxHistory) {
        redo_stack_.erase(redo_stack_.begin());
    }

    *restored_state = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    return true;
}

bool HistoryManager::Redo(State current_state, State* restored_state) {
    EndTypingGroup();
    if (redo_stack_.empty() || restored_state == nullptr) {
        return false;
    }

    undo_stack_.push_back(std::move(current_state));
    if (undo_stack_.size() > kMaxHistory) {
        undo_stack_.erase(undo_stack_.begin());
    }

    *restored_state = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    return true;
}

} // namespace textlt
