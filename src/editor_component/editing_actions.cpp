    void EditorComponent::ToggleComment() {
        if (!doc_ || doc_->lines.empty()) {
            return;
        }

        EndTypingGroup();
        ClampCursorToBuffer();
        SaveSnapshot();

        size_t start_row = doc_->cursor_row;
        size_t end_row = doc_->cursor_row;
        if (HasSelection()) {
            auto [start, end] = utils::OrderedSelection(
                {doc_->selection.anchor_x, doc_->selection.anchor_y},
                {doc_->cursor_col, doc_->cursor_row},
                doc_->lines);
            start_row = start.y;
            end_row = end.y;
            if (end.x == 0 && end_row > start_row) {
                --end_row;
            }
        }

        const std::string prefix = GetCommentPrefix();
        for (size_t row = start_row; row <= end_row; ++row) {
            std::string& line = doc_->lines[row];
            const size_t first_text = line.find_first_not_of(" \t");
            const size_t insert_position =
            first_text == std::string::npos ? line.size() : first_text;

            if (line.compare(insert_position, prefix.size(), prefix) == 0) {
                line.erase(insert_position, prefix.size());
                if (insert_position < line.size() && line[insert_position] == ' ') {
                    line.erase(insert_position, 1);
                }
            } else {
                line.insert(insert_position, prefix + " ");
            }
        }

        doc_->is_dirty = true;
        ClampCursorToBuffer();
        UpdateScroll();
    }

    void EditorComponent::ClearSelection() {
        if (doc_) doc_->ClearSelection();
    }

    void EditorComponent::InsertText(const std::string& text, bool keep_cursor) {
        if (text.empty() || !doc_) {
            return;
        }

        EndTypingGroup();
        size_t insertion_row = doc_->cursor_row;
        size_t insertion_col = doc_->cursor_col;
        if (keep_cursor && doc_->HasSelection()) {
            const auto ordered_selection = utils::OrderedSelection(
                {doc_->selection.anchor_x, doc_->selection.anchor_y},
                {doc_->cursor_col, doc_->cursor_row},
                doc_->lines);
            insertion_row = ordered_selection.first.y;
            insertion_col = ordered_selection.first.x;
        }
        if (doc_->InsertText(text)) {
            ClearSelection();
            if (keep_cursor) {
                doc_->SetCursorPosition(insertion_row, insertion_col);
            }
            UpdateScroll();
        }
    }

    bool EditorComponent::HandleAutoPairCharacter(const std::string& input) {
        if (input.size() != 1 || !doc_) {
            return false;
        }

        const char character = input.front();
        if (IsPairClosingCharacter(character) &&
            doc_->cursor_col < doc_->lines[doc_->cursor_row].size() &&
            doc_->lines[doc_->cursor_row][doc_->cursor_col] == character) {
            EndTypingGroup();
            doc_->cursor_col++;
            ClearSelection();
            UpdateScroll();
            return true;
        }

        if (!IsPairOpeningCharacter(character)) {
            return false;
        }

        const char closing = ClosingPairFor(character);
        if (closing == '\0') {
            return false;
        }

        if (doc_->InsertPairedCharacter(character, closing)) {
            ClearSelection();
            UpdateScroll();
            return true;
        }

        return false;
    }

    bool EditorComponent::HandleAutoIndentReturn() {
        EndTypingGroup();
        SaveSnapshot();
        if (HasSelection()) {
            DeleteSelectionWithoutSnapshot();
        }

        if (!doc_) return false;

        const std::string line_before_cursor = doc_->lines[doc_->cursor_row].substr(0, doc_->cursor_col);
        std::string indent;
        if (!config_ || config_->auto_indent) {
            const int configured_tab_size = config_ ? config_->tab_size : 4;
            const size_t tab_size = configured_tab_size == 2 ? 2 : 4;
            indent = LeadingIndent(line_before_cursor);
            const std::string trimmed_before_cursor = TrimRightWhitespace(line_before_cursor);

            if (!trimmed_before_cursor.empty() &&
                (trimmed_before_cursor.back() == '{' ||
                trimmed_before_cursor.back() == ':' ||
                EndsWithRubyDo(trimmed_before_cursor))) {
                indent += std::string(tab_size, ' ');
                }
        }

        std::string next_line = doc_->lines[doc_->cursor_row].substr(doc_->cursor_col);
        doc_->lines[doc_->cursor_row].erase(doc_->cursor_col);
        doc_->lines.insert(doc_->lines.begin() + doc_->cursor_row + 1, indent + next_line);
        doc_->cursor_row++;
        doc_->cursor_col = indent.size();
        doc_->is_dirty = true;
        ClearSelection();
        UpdateScroll();
        return true;
    }

    void EditorComponent::ConvertTabsToSpaces() {
        if (!doc_) return;
        const int configured_tab_size = config_ ? config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;

        EndTypingGroup();
        ClampCursorToBuffer();
        if (doc_->ConvertTabsToSpaces(tab_size)) {
            ClearSelection();
            UpdateScroll();
        }
    }

    void EditorComponent::Convert4To2Spaces() {
        if (!doc_) return;
        EndTypingGroup();
        ClampCursorToBuffer();
        if (doc_->Convert4To2Spaces()) {
            UpdateScroll();
        }
    }

    void EditorComponent::Convert2To4Spaces() {
        if (!doc_) return;
        EndTypingGroup();
        ClampCursorToBuffer();
        if (doc_->Convert2To4Spaces()) {
            UpdateScroll();
        }
    }

    bool EditorComponent::IndentLines() {
        if (!doc_) return false;
        const int configured_tab_size = config_ ? config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;

        EndTypingGroup();
        ClampCursorToBuffer();
        const bool changed = doc_->IndentLines(tab_size);
        if (changed) {
            UpdateScroll();
        }
        return changed;
    }

    bool EditorComponent::OutdentLines() {
        if (!doc_) return false;
        const int configured_tab_size = config_ ? config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;

        EndTypingGroup();
        ClampCursorToBuffer();
        const bool changed = doc_->OutdentLines(tab_size);
        if (changed) {
            UpdateScroll();
        }
        return changed;
    }

    void EditorComponent::ToggleCase() {
        if (!doc_) return;
        EndTypingGroup();
        ClampCursorToBuffer();
        if (doc_->ToggleCase()) {
            UpdateScroll();
        }
    }
