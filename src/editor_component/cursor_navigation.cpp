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
            utils::BuildUtf8WrapSegments(session_->lines[session_->cursor_row], width);
        size_t segment_index = current_segments.size() - 1;
        for (size_t index = 0; index < current_segments.size(); ++index) {
            if (session_->cursor_col < current_segments[index].end ||
                index + 1 == current_segments.size()) {
                segment_index = index;
                break;
            }
        }

        const size_t display_column = utils::Utf8DisplayWidth(
            session_->lines[session_->cursor_row],
            current_segments[segment_index].start,
            session_->cursor_col);

        utils::Utf8WrapSegment target;
        if (segment_index > 0) {
            target = current_segments[segment_index - 1];
        } else {
            if (session_->cursor_row == 0) return;
            --session_->cursor_row;
            const auto target_segments =
                utils::BuildUtf8WrapSegments(session_->lines[session_->cursor_row], width);
            target = target_segments.back();
        }
        session_->cursor_col = std::min(
            target.end,
            utils::Utf8ByteIndexAtDisplayColumn(
                session_->lines[session_->cursor_row], target.start, display_column));
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
            utils::BuildUtf8WrapSegments(session_->lines[session_->cursor_row], width);
        size_t segment_index = current_segments.size() - 1;
        for (size_t index = 0; index < current_segments.size(); ++index) {
            if (session_->cursor_col < current_segments[index].end ||
                index + 1 == current_segments.size()) {
                segment_index = index;
                break;
            }
        }

        const size_t display_column = utils::Utf8DisplayWidth(
            session_->lines[session_->cursor_row],
            current_segments[segment_index].start,
            session_->cursor_col);

        utils::Utf8WrapSegment target;
        if (segment_index + 1 < current_segments.size()) {
            target = current_segments[segment_index + 1];
        } else {
            if (session_->cursor_row + 1 >= session_->lines.size()) return;
            ++session_->cursor_row;
            const auto target_segments =
                utils::BuildUtf8WrapSegments(session_->lines[session_->cursor_row], width);
            target = target_segments.front();
        }
        session_->cursor_col = std::min(
            target.end,
            utils::Utf8ByteIndexAtDisplayColumn(
                session_->lines[session_->cursor_row], target.start, display_column));
    }

    void EditorComponent::MoveCursorPageUp() {
        ClampCursorToBuffer();
        if (!session_) return;
        const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
        session_->cursor_row = session_->cursor_row > page_step ? session_->cursor_row - page_step : 0;
        session_->cursor_col = std::min(session_->cursor_col, session_->lines[session_->cursor_row].size());
    }

    void EditorComponent::MoveCursorPageDown() {
        ClampCursorToBuffer();
        if (!session_) return;
        const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
        session_->cursor_row = std::min(session_->cursor_row + page_step, session_->lines.size() - 1);
        session_->cursor_col = std::min(session_->cursor_col, session_->lines[session_->cursor_row].size());
    }

    void EditorComponent::MoveCursorToPreviousParagraph() {
        if (session_) session_->MoveCursorToPreviousParagraph();
    }

    void EditorComponent::MoveCursorToNextParagraph() {
        if (session_) session_->MoveCursorToNextParagraph();
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
