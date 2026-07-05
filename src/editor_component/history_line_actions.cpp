    bool EditorComponent::MoveLinesUp() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (!doc_) return false;
        const bool changed = doc_->MoveLinesUp();
        if (changed) UpdateScroll();
        return changed;
    }

    bool EditorComponent::MoveLinesDown() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (!doc_) return false;
        const bool changed = doc_->MoveLinesDown();
        if (changed) UpdateScroll();
        return changed;
    }

    bool EditorComponent::DuplicateLines() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (!doc_) return false;

        const bool changed = doc_->DuplicateLines();
        if (changed) {
            UpdateScroll();
        }
        return changed;
    }

    HistoryManager::State EditorComponent::CurrentState() const {
        return doc_ ? doc_->CurrentState() : HistoryManager::State{};
    }

    void EditorComponent::ApplyState(const HistoryManager::State& state) {
        if (!doc_) return;
        doc_->ApplyState(state);
        ClearSelection();
        UpdateScroll();
    }

    void EditorComponent::SaveSnapshot() {
        if (doc_) doc_->SaveSnapshot();
    }

    void EditorComponent::SaveSnapshotForTyping(const std::string& input) {
        if (doc_) doc_->SaveSnapshotForTyping(input, HasSelection());
    }

    void EditorComponent::EndTypingGroup() {
        if (doc_) doc_->EndTypingGroup();
    }

    void EditorComponent::Undo() {
        if (doc_ && doc_->Undo()) {
            ClearSelection();
            UpdateScroll();
        }
    }

    void EditorComponent::Redo() {
        if (doc_ && doc_->Redo()) {
            ClearSelection();
            UpdateScroll();
        }
    }

    std::string EditorComponent::GetCurrentLineText() const {
        return doc_ ? doc_->CurrentLineText() : "";
    }

    void EditorComponent::DeleteCurrentLine() {
        if (!doc_) return;
        EndTypingGroup();
        if (doc_->DeleteCurrentLine()) {
            ClearSelection();
            UpdateScroll();
        }
    }
