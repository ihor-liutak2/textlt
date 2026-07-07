    void EditorComponent::ToggleComment() {
        if (!session_ || session_->lines.empty()) {
            return;
        }

        EndTypingGroup();
        ClampCursorToBuffer();
        SaveSnapshot();

        size_t start_row = session_->CursorRow();
        size_t end_row = session_->CursorRow();
        if (HasSelection()) {
            auto [start, end] = utils::OrderedSelection(
                {session_->SelectionState().anchor_x, session_->SelectionState().anchor_y},
                {session_->CursorCol(), session_->CursorRow()},
                session_->lines);
            start_row = start.y;
            end_row = end.y;
            if (end.x == 0 && end_row > start_row) {
                --end_row;
            }
        }

        const std::string prefix = GetCommentPrefix();
        for (size_t row = start_row; row <= end_row; ++row) {
            std::string& line = session_->lines[row];
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

        session_->is_dirty = true;
        ClampCursorToBuffer();
        UpdateScroll();
    }

    void EditorComponent::ClearSelection() {
        if (session_) session_->ClearSelection();
    }

    void EditorComponent::InsertText(const std::string& text, bool keep_cursor) {
        if (text.empty() || !session_) {
            return;
        }

        EndTypingGroup();
        size_t insertion_row = session_->CursorRow();
        size_t insertion_col = session_->CursorCol();
        if (keep_cursor && session_->HasSelection()) {
            const auto ordered_selection = utils::OrderedSelection(
                {session_->SelectionState().anchor_x, session_->SelectionState().anchor_y},
                {session_->CursorCol(), session_->CursorRow()},
                session_->lines);
            insertion_row = ordered_selection.first.y;
            insertion_col = ordered_selection.first.x;
        }
        if (session_->InsertText(text)) {
            ClearSelection();
            if (keep_cursor) {
                session_->SetCursorPosition(insertion_row, insertion_col);
            }
            UpdateScroll();
        }
    }

    bool EditorComponent::HandleAutoPairCharacter(const std::string& input) {
        if (input.size() != 1 || !session_) {
            return false;
        }

        const char character = input.front();
        if (IsPairClosingCharacter(character) &&
            session_->CursorCol() < session_->lines[session_->CursorRow()].size() &&
            session_->lines[session_->CursorRow()][session_->CursorCol()] == character) {
            EndTypingGroup();
            session_->CursorCol()++;
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

        if (session_->InsertPairedCharacter(character, closing)) {
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

        if (!session_) return false;

        const std::string line_before_cursor = session_->lines[session_->CursorRow()].substr(0, session_->CursorCol());
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

        std::string next_line = session_->lines[session_->CursorRow()].substr(session_->CursorCol());
        session_->lines[session_->CursorRow()].erase(session_->CursorCol());
        session_->lines.insert(session_->lines.begin() + session_->CursorRow() + 1, indent + next_line);
        session_->CursorRow()++;
        session_->CursorCol() = indent.size();
        session_->is_dirty = true;
        ClearSelection();
        UpdateScroll();
        return true;
    }

    void EditorComponent::ConvertTabsToSpaces() {
        if (!session_) return;
        const int configured_tab_size = config_ ? config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;

        EndTypingGroup();
        ClampCursorToBuffer();
        if (session_->ConvertTabsToSpaces(tab_size)) {
            ClearSelection();
            UpdateScroll();
        }
    }

    void EditorComponent::Convert4To2Spaces() {
        if (!session_) return;
        EndTypingGroup();
        ClampCursorToBuffer();
        if (session_->Convert4To2Spaces()) {
            UpdateScroll();
        }
    }

    void EditorComponent::Convert2To4Spaces() {
        if (!session_) return;
        EndTypingGroup();
        ClampCursorToBuffer();
        if (session_->Convert2To4Spaces()) {
            UpdateScroll();
        }
    }

    bool EditorComponent::IndentLines() {
        if (!session_) return false;
        const int configured_tab_size = config_ ? config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;

        EndTypingGroup();
        ClampCursorToBuffer();
        const bool changed = session_->IndentLines(tab_size);
        if (changed) {
            UpdateScroll();
        }
        return changed;
    }

    bool EditorComponent::OutdentLines() {
        if (!session_) return false;
        const int configured_tab_size = config_ ? config_->tab_size : 4;
        const size_t tab_size = configured_tab_size == 2 ? 2 : 4;

        EndTypingGroup();
        ClampCursorToBuffer();
        const bool changed = session_->OutdentLines(tab_size);
        if (changed) {
            UpdateScroll();
        }
        return changed;
    }

    void EditorComponent::ToggleCase() {
        if (!session_) return;
        EndTypingGroup();
        ClampCursorToBuffer();
        if (session_->ToggleCase()) {
            UpdateScroll();
        }
    }
