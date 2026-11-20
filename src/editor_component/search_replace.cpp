    void EditorComponent::HighlightMatches(const std::string& query) {
        search_matches_.clear();
        current_search_match_ = 0;

        if (query.empty() || !session_) {
            return;
        }

        const std::string needle = search_match_case_ ? query : ToLowerCopy(query);
        for (size_t y = 0; y < session_->lines.size(); ++y) {
            const std::string& line = session_->lines[y];
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
        if (search_matches_.empty() || !session_) {
            return;
        }

        current_search_match_ = 0;
        for (size_t i = 0; i < search_matches_.size(); ++i) {
            const SearchMatch& match = search_matches_[i];
            if (match.y > session_->CursorRow() || (match.y == session_->CursorRow() && match.x > session_->CursorCol())) {
                current_search_match_ = i;
                break;
            }
        }
        MoveCursorToSearchMatch(search_matches_[current_search_match_]);
    }

    void EditorComponent::JumpToPreviousMatch() {
        if (search_matches_.empty() || !session_) {
            return;
        }

        current_search_match_ = search_matches_.size() - 1;
        for (size_t i = search_matches_.size(); i-- > 0;) {
            const SearchMatch& match = search_matches_[i];
            if (match.y < session_->CursorRow() || (match.y == session_->CursorRow() && match.x < session_->CursorCol())) {
                current_search_match_ = i;
                break;
            }
        }
        MoveCursorToSearchMatch(search_matches_[current_search_match_]);
    }

    void EditorComponent::ExecuteReplaceNext(const std::string& query, const std::string& replacement) {
        HighlightMatches(query);
        if (search_matches_.empty() || !session_) {
            return;
        }

        const SearchMatch match = search_matches_[current_search_match_];
        SaveSnapshot();
        session_->lines[match.y].replace(match.x, match.length, replacement);
        session_->CursorRow() = match.y;
        session_->CursorCol() = match.x + replacement.size();
        ClearSelection();
        HighlightMatches(query);
        UpdateScroll();
    }

    void EditorComponent::ExecuteReplaceAll(const std::string& query, const std::string& replacement) {
        HighlightMatches(query);
        if (search_matches_.empty() || !session_) {
            return;
        }

        SaveSnapshot();
        for (size_t index = search_matches_.size(); index-- > 0;) {
            const SearchMatch& match = search_matches_[index];
            session_->lines[match.y].replace(match.x, match.length, replacement);
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

    void EditorComponent::SetSearchOptions(bool match_case, bool whole_word) {
        search_match_case_ = match_case;
        search_whole_word_ = whole_word;
        if (config_) {
            config_->search_match_case = match_case;
            config_->search_whole_word = whole_word;
        }
    }

    std::vector<EditorComponent::SearchResult> EditorComponent::FindAll(
        const std::string& query, bool match_case, bool whole_word,
        size_t limit, bool& truncated) {
        truncated = false;
        std::vector<SearchResult> results;
        search_matches_.clear();
        current_search_match_ = 0;
        SetSearchOptions(match_case, whole_word);
        if (query.empty() || !session_) {
            return results;
        }

        const std::string needle =
            match_case ? query : ToLowerCopy(query);
        for (size_t y = 0; y < session_->lines.size(); ++y) {
            const std::string& line = session_->lines[y];
            const std::string searchable_line =
                match_case ? line : ToLowerCopy(line);
            size_t position = searchable_line.find(needle);
            while (position != std::string::npos) {
                const bool has_word_before = position > 0 &&
                    utils::IsWordCharacter(line[position - 1]);
                const size_t after_match = position + needle.size();
                const bool has_word_after = after_match < line.size() &&
                    utils::IsWordCharacter(line[after_match]);
                if (!whole_word || (!has_word_before && !has_word_after)) {
                    search_matches_.push_back({position, y, needle.size()});
                    if (results.size() < limit) {
                        SearchResult result;
                        result.row = y;
                        result.column = position;
                        result.length = needle.size();
                        result.preview = line;
                        result.preview_match_start = position;
                        result.preview_match_length = needle.size();
                        results.push_back(std::move(result));
                    } else {
                        truncated = true;
                    }
                }
                position = searchable_line.find(
                    needle, position + std::max<size_t>(1, needle.size()));
            }
        }
        return results;
    }

    void EditorComponent::ActivateSearchResult(size_t index) {
        if (index >= search_matches_.size() || !session_) return;
        current_search_match_ = index;
        MoveCursorToSearchMatch(search_matches_[index]);
    }

    bool EditorComponent::ReplaceSearchResult(size_t index,
        const std::string& replacement, std::string& error) {
        if (!session_) {
            error = "No active document";
            return false;
        }
        if (session_->read_only) {
            error = "Document is read-only";
            return false;
        }
        if (index >= search_matches_.size()) {
            error = "No match to replace";
            return false;
        }
        const SearchMatch match = search_matches_[index];
        if (match.y >= session_->lines.size()) {
            error = "The match is no longer available";
            return false;
        }
        SaveSnapshot();
        std::string& line = session_->lines[match.y];
        const size_t from = std::min(match.x, line.size());
        const size_t length = std::min(match.length, line.size() - from);
        line.replace(from, length, replacement);
        session_->CursorRow() = match.y;
        session_->CursorCol() = from + replacement.size();
        session_->buffer.MarkDirty();
        ClearSelection();
        search_matches_.clear();
        current_search_match_ = 0;
        UpdateScroll();
        return true;
    }

    size_t EditorComponent::ReplaceAllSearchMatches(const std::string& query,
        const std::string& replacement, bool match_case, bool whole_word) {
        if (!session_ || session_->read_only || query.empty()) return 0;
        bool truncated = false;
        FindAll(query, match_case, whole_word, 1, truncated);
        if (search_matches_.empty()) return 0;
        const size_t count = search_matches_.size();
        SaveSnapshot();
        for (size_t index = search_matches_.size(); index-- > 0;) {
            const SearchMatch& match = search_matches_[index];
            if (match.y >= session_->lines.size()) continue;
            std::string& line = session_->lines[match.y];
            const size_t from = std::min(match.x, line.size());
            const size_t length = std::min(match.length, line.size() - from);
            line.replace(from, length, replacement);
        }
        session_->ClampCursor();
        session_->buffer.MarkDirty();
        ClearSelection();
        search_matches_.clear();
        current_search_match_ = 0;
        UpdateScroll();
        return count;
    }
