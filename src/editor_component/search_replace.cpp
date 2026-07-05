    void EditorComponent::HighlightMatches(const std::string& query) {
        search_matches_.clear();
        current_search_match_ = 0;

        if (query.empty() || !doc_) {
            return;
        }

        const std::string needle = search_match_case_ ? query : ToLowerCopy(query);
        for (size_t y = 0; y < doc_->lines.size(); ++y) {
            const std::string& line = doc_->lines[y];
            const std::string searchable_line =
            search_match_case_ ? line : ToLowerCopy(line);
            size_t position = searchable_line.find(needle);

            while (position != std::string::npos) {
                const bool has_word_before =
                position > 0 && utils::IsWordCharacter(line[position - 1]);
                const size_t after_match = position + query.size();
                const bool has_word_after =
                after_match < line.size() && utils::IsWordCharacter(line[after_match]);

                if (!search_whole_word_ || (!has_word_before && !has_word_after)) {
                    search_matches_.push_back({position, y, query.size()});
                }

                position = searchable_line.find(needle, position + query.size());
            }
        }

        if (!search_matches_.empty()) {
            current_search_match_ = FindMatchAtOrAfterCursor();
        }
    }

    void EditorComponent::ClearSearchHighlights() {
        search_matches_.clear();
        current_search_match_ = 0;
    }

    void EditorComponent::JumpToNextMatch() {
        if (search_matches_.empty() || !doc_) {
            return;
        }

        current_search_match_ = 0;
        for (size_t i = 0; i < search_matches_.size(); ++i) {
            const SearchMatch& match = search_matches_[i];
            if (match.y > doc_->cursor_row || (match.y == doc_->cursor_row && match.x > doc_->cursor_col)) {
                current_search_match_ = i;
                break;
            }
        }
        MoveCursorToSearchMatch(search_matches_[current_search_match_]);
    }

    void EditorComponent::JumpToPreviousMatch() {
        if (search_matches_.empty() || !doc_) {
            return;
        }

        current_search_match_ = search_matches_.size() - 1;
        for (size_t i = search_matches_.size(); i-- > 0;) {
            const SearchMatch& match = search_matches_[i];
            if (match.y < doc_->cursor_row || (match.y == doc_->cursor_row && match.x < doc_->cursor_col)) {
                current_search_match_ = i;
                break;
            }
        }
        MoveCursorToSearchMatch(search_matches_[current_search_match_]);
    }

    void EditorComponent::ExecuteReplaceNext(const std::string& query, const std::string& replacement) {
        HighlightMatches(query);
        if (search_matches_.empty() || !doc_) {
            return;
        }

        const SearchMatch match = search_matches_[current_search_match_];
        SaveSnapshot();
        doc_->lines[match.y].replace(match.x, match.length, replacement);
        doc_->cursor_row = match.y;
        doc_->cursor_col = match.x + replacement.size();
        ClearSelection();
        HighlightMatches(query);
        UpdateScroll();
    }

    void EditorComponent::ExecuteReplaceAll(const std::string& query, const std::string& replacement) {
        HighlightMatches(query);
        if (search_matches_.empty() || !doc_) {
            return;
        }

        SaveSnapshot();
        for (size_t index = search_matches_.size(); index-- > 0;) {
            const SearchMatch& match = search_matches_[index];
            doc_->lines[match.y].replace(match.x, match.length, replacement);
        }

        ClampCursorToBuffer();
        ClearSelection();
        HighlightMatches(query);
        UpdateScroll();
    }

    void EditorComponent::ToggleSearchMatchCase() {
        search_match_case_ = !search_match_case_;
        if (config_) {
            config_->search_match_case = search_match_case_;
        }
    }

    void EditorComponent::ToggleSearchWholeWord() {
        search_whole_word_ = !search_whole_word_;
        if (config_) {
            config_->search_whole_word = search_whole_word_;
        }
    }

    bool EditorComponent::SearchMatchCase() const {
        return search_match_case_;
    }

    bool EditorComponent::SearchWholeWord() const {
        return search_whole_word_;
    }

    size_t EditorComponent::SearchMatchCount() const {
        return search_matches_.size();
    }

    size_t EditorComponent::CurrentSearchMatchIndex() const {
        if (search_matches_.empty()) {
            return 0;
        }
        return current_search_match_ + 1;
    }
