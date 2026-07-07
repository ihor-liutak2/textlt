    bool EditorComponent::MoveLinesUp() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (!session_) return false;
        const bool changed = session_->MoveLinesUp();
        if (changed) UpdateScroll();
        return changed;
    }

    bool EditorComponent::MoveLinesDown() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (!session_) return false;
        const bool changed = session_->MoveLinesDown();
        if (changed) UpdateScroll();
        return changed;
    }

    bool EditorComponent::DuplicateLines() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (!session_) return false;

        const bool changed = session_->DuplicateLines();
        if (changed) {
            UpdateScroll();
        }
        return changed;
    }

    HistoryManager::State EditorComponent::CurrentState() const {
        return session_ ? session_->CurrentState() : HistoryManager::State{};
    }

    void EditorComponent::ApplyState(const HistoryManager::State& state) {
        if (!session_) return;
        session_->ApplyState(state);
        ClearSelection();
        UpdateScroll();
    }

    void EditorComponent::SaveSnapshot() {
        if (session_) session_->SaveSnapshot();
    }

    void EditorComponent::SaveSnapshotForTyping(const std::string& input) {
        if (session_) session_->SaveSnapshotForTyping(input, HasSelection());
    }

    void EditorComponent::EndTypingGroup() {
        if (session_) session_->EndTypingGroup();
    }

    void EditorComponent::Undo() {
        if (session_ && session_->Undo()) {
            ClearSelection();
            UpdateScroll();
        }
    }

    void EditorComponent::Redo() {
        if (session_ && session_->Redo()) {
            ClearSelection();
            UpdateScroll();
        }
    }

    std::string EditorComponent::GetCurrentLineText() const {
        return session_ ? session_->CurrentLineText() : "";
    }

    void EditorComponent::DeleteCurrentLine() {
        if (!session_) return;
        EndTypingGroup();
        if (session_->DeleteCurrentLine()) {
            ClearSelection();
            UpdateScroll();
        }
    }
