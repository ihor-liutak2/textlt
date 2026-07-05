    void EditorComponent::ClampCursorToBuffer() {
        if (!doc_) return;
        doc_->ClampCursor();
    }

    void EditorComponent::MoveCursorHome() {
        if (doc_) doc_->MoveCursorHome();
    }

    void EditorComponent::MoveCursorEnd() {
        if (doc_) doc_->MoveCursorEnd();
    }

    void EditorComponent::MoveCursorLeft() {
        if (doc_) doc_->MoveCursorLeft();
    }

    void EditorComponent::MoveCursorRight() {
        if (doc_) doc_->MoveCursorRight();
    }

    void EditorComponent::MoveCursorUp() {
        if (!doc_) return;
        if (!config_ || !config_->smart_word_wrap) {
            doc_->MoveCursorUp();
            return;
        }

        doc_->ClampCursor();
        const size_t width = VisibleTextWidth();
        const auto current_segments =
            utils::BuildUtf8WrapSegments(doc_->lines[doc_->cursor_row], width);
        size_t segment_index = current_segments.size() - 1;
        for (size_t index = 0; index < current_segments.size(); ++index) {
            if (doc_->cursor_col < current_segments[index].end ||
                index + 1 == current_segments.size()) {
                segment_index = index;
                break;
            }
        }

        const size_t display_column = utils::Utf8DisplayWidth(
            doc_->lines[doc_->cursor_row],
            current_segments[segment_index].start,
            doc_->cursor_col);

        utils::Utf8WrapSegment target;
        if (segment_index > 0) {
            target = current_segments[segment_index - 1];
        } else {
            if (doc_->cursor_row == 0) return;
            --doc_->cursor_row;
            const auto target_segments =
                utils::BuildUtf8WrapSegments(doc_->lines[doc_->cursor_row], width);
            target = target_segments.back();
        }
        doc_->cursor_col = std::min(
            target.end,
            utils::Utf8ByteIndexAtDisplayColumn(
                doc_->lines[doc_->cursor_row], target.start, display_column));
    }

    void EditorComponent::MoveCursorDown() {
        if (!doc_) return;
        if (!config_ || !config_->smart_word_wrap) {
            doc_->MoveCursorDown();
            return;
        }

        doc_->ClampCursor();
        const size_t width = VisibleTextWidth();
        const auto current_segments =
            utils::BuildUtf8WrapSegments(doc_->lines[doc_->cursor_row], width);
        size_t segment_index = current_segments.size() - 1;
        for (size_t index = 0; index < current_segments.size(); ++index) {
            if (doc_->cursor_col < current_segments[index].end ||
                index + 1 == current_segments.size()) {
                segment_index = index;
                break;
            }
        }

        const size_t display_column = utils::Utf8DisplayWidth(
            doc_->lines[doc_->cursor_row],
            current_segments[segment_index].start,
            doc_->cursor_col);

        utils::Utf8WrapSegment target;
        if (segment_index + 1 < current_segments.size()) {
            target = current_segments[segment_index + 1];
        } else {
            if (doc_->cursor_row + 1 >= doc_->lines.size()) return;
            ++doc_->cursor_row;
            const auto target_segments =
                utils::BuildUtf8WrapSegments(doc_->lines[doc_->cursor_row], width);
            target = target_segments.front();
        }
        doc_->cursor_col = std::min(
            target.end,
            utils::Utf8ByteIndexAtDisplayColumn(
                doc_->lines[doc_->cursor_row], target.start, display_column));
    }

    void EditorComponent::MoveCursorPageUp() {
        ClampCursorToBuffer();
        if (!doc_) return;
        const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
        doc_->cursor_row = doc_->cursor_row > page_step ? doc_->cursor_row - page_step : 0;
        doc_->cursor_col = std::min(doc_->cursor_col, doc_->lines[doc_->cursor_row].size());
    }

    void EditorComponent::MoveCursorPageDown() {
        ClampCursorToBuffer();
        if (!doc_) return;
        const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
        doc_->cursor_row = std::min(doc_->cursor_row + page_step, doc_->lines.size() - 1);
        doc_->cursor_col = std::min(doc_->cursor_col, doc_->lines[doc_->cursor_row].size());
    }

    void EditorComponent::MoveCursorToPreviousParagraph() {
        if (doc_) doc_->MoveCursorToPreviousParagraph();
    }

    void EditorComponent::MoveCursorToNextParagraph() {
        if (doc_) doc_->MoveCursorToNextParagraph();
    }

    void EditorComponent::MoveCursorToPreviousWord() {
        if (doc_) doc_->MoveCursorToPreviousWord();
    }

    void EditorComponent::MoveCursorToNextWord() {
        if (doc_) doc_->MoveCursorToNextWord();
    }

    void EditorComponent::DeleteWordBackward() {
        EndTypingGroup();
        ClampCursorToBuffer();
        if (HasSelection()) {
            DeleteSelection();
            return;
        }
        if (!doc_) return;

        if (doc_->DeleteWordBackward()) {
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
        if (!doc_) return;

        if (doc_->DeleteWordForward()) {
            ClearSelection();
            UpdateScroll();
        }
    }
