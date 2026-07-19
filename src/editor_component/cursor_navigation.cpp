    void EditorComponent::ClampCursorToBuffer() {
        if (!session_) return;
        session_->ClampCursor();
    }

    void EditorComponent::MoveCursorHome() {
        if (session_) session_->MoveCursorHome();
    }

    void EditorComponent::MoveCursorEnd() {
        if (session_) session_->MoveCursorEnd();
    }

    void EditorComponent::MoveCursorDocumentStart() {
        if (session_) session_->MoveCursorDocumentStart();
    }

    void EditorComponent::MoveCursorDocumentEnd() {
        if (session_) session_->MoveCursorDocumentEnd();
    }

    void EditorComponent::MoveCursorLeft() {
        if (session_) session_->MoveCursorLeft();
    }

    void EditorComponent::MoveCursorRight() {
        if (session_) session_->MoveCursorRight();
    }

    void EditorComponent::MoveCursorUp() {
        if (!session_) return;
        if (!config_ || !config_->smart_word_wrap) {
            session_->MoveCursorUp();
            return;
        }

        session_->ClampCursor();
        const size_t width = VisibleTextWidth();
        const auto current_segments =
            utils::BuildUtf8WrapSegments(session_->lines[session_->CursorRow()], width);
        size_t segment_index = current_segments.size() - 1;
        for (size_t index = 0; index < current_segments.size(); ++index) {
            if (session_->CursorCol() < current_segments[index].end ||
                index + 1 == current_segments.size()) {
                segment_index = index;
                break;
            }
        }

        const size_t display_column = utils::Utf8DisplayWidth(
            session_->lines[session_->CursorRow()],
            current_segments[segment_index].start,
            session_->CursorCol());

        utils::Utf8WrapSegment target;
        if (segment_index > 0) {
            target = current_segments[segment_index - 1];
        } else {
            if (session_->CursorRow() == 0) return;
            --session_->CursorRow();
            const auto target_segments =
                utils::BuildUtf8WrapSegments(session_->lines[session_->CursorRow()], width);
            target = target_segments.back();
        }
        session_->CursorCol() = std::min(
            target.end,
            utils::Utf8ByteIndexAtDisplayColumn(
                session_->lines[session_->CursorRow()], target.start, display_column));
    }

    void EditorComponent::MoveCursorDown() {
        if (!session_) return;
        if (!config_ || !config_->smart_word_wrap) {
            session_->MoveCursorDown();
            return;
        }

        session_->ClampCursor();
        const size_t width = VisibleTextWidth();
        const auto current_segments =
            utils::BuildUtf8WrapSegments(session_->lines[session_->CursorRow()], width);
        size_t segment_index = current_segments.size() - 1;
        for (size_t index = 0; index < current_segments.size(); ++index) {
            if (session_->CursorCol() < current_segments[index].end ||
                index + 1 == current_segments.size()) {
                segment_index = index;
                break;
            }
        }

        const size_t display_column = utils::Utf8DisplayWidth(
            session_->lines[session_->CursorRow()],
            current_segments[segment_index].start,
            session_->CursorCol());

        utils::Utf8WrapSegment target;
        if (segment_index + 1 < current_segments.size()) {
            target = current_segments[segment_index + 1];
        } else {
            if (session_->CursorRow() + 1 >= session_->lines.size()) return;
            ++session_->CursorRow();
            const auto target_segments =
                utils::BuildUtf8WrapSegments(session_->lines[session_->CursorRow()], width);
            target = target_segments.front();
        }
        session_->CursorCol() = std::min(
            target.end,
            utils::Utf8ByteIndexAtDisplayColumn(
                session_->lines[session_->CursorRow()], target.start, display_column));
    }

    void EditorComponent::MoveCursorPageUp() {
        ClampCursorToBuffer();
        if (!session_) return;
        const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
        session_->CursorRow() = session_->CursorRow() > page_step ? session_->CursorRow() - page_step : 0;
        session_->CursorCol() = std::min(session_->CursorCol(), session_->lines[session_->CursorRow()].size());
    }

    void EditorComponent::MoveCursorPageDown() {
        ClampCursorToBuffer();
        if (!session_) return;
        const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
        session_->CursorRow() = std::min(session_->CursorRow() + page_step, session_->lines.size() - 1);
        session_->CursorCol() = std::min(session_->CursorCol(), session_->lines[session_->CursorRow()].size());
    }

    void EditorComponent::MoveCursorToPreviousParagraph() {
        if (session_) session_->MoveCursorToPreviousParagraph();
    }

    void EditorComponent::MoveCursorToNextParagraph() {
        if (session_) session_->MoveCursorToNextParagraph();
    }

    void EditorComponent::MoveCursorToPreviousParagraphSelectionBoundary() {
        if (session_) session_->MoveCursorToPreviousParagraphSelectionBoundary();
    }

    void EditorComponent::MoveCursorToNextParagraphSelectionBoundary() {
        if (session_) session_->MoveCursorToNextParagraphSelectionBoundary();
    }

    void EditorComponent::SelectCurrentLine() {
        EndTypingGroup();
        if (session_) {
            session_->SelectCurrentLine();
            UpdateScroll();
        }
    }

    void EditorComponent::ToggleSelectionAnchor() {
        EndTypingGroup();
        if (session_) {
            session_->ToggleSelectionAnchor();
            UpdateScroll();
        }
    }

    void EditorComponent::MoveCursorToPreviousWord() {
        if (session_) session_->MoveCursorToPreviousWord();
    }

    void EditorComponent::MoveCursorToNextWord() {
        if (session_) session_->MoveCursorToNextWord();
    }

    void EditorComponent::DeleteWordBackward() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (HasSelection()) {
            DeleteSelection();
            return;
        }
        if (!session_) return;

        if (session_->DeleteWordBackward()) {
            ClearSelection();
            UpdateScroll();
        }
    }

    void EditorComponent::DeleteWordForward() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (HasSelection()) {
            DeleteSelection();
            return;
        }
        if (!session_) return;

        if (session_->DeleteWordForward()) {
            ClearSelection();
            UpdateScroll();
        }
    }
