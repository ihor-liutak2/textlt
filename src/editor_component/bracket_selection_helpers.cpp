    std::optional<std::pair<size_t, size_t>> EditorComponent::FindBracketNearCursor() const {
        if (!doc_ || doc_->lines.empty() || doc_->cursor_row >= doc_->lines.size()) {
            return std::nullopt;
        }

        const std::string& line = doc_->lines[doc_->cursor_row];

        if (doc_->cursor_col > 0 && doc_->cursor_col - 1 < line.size() &&
            utils::IsBracketCharacter(line[doc_->cursor_col - 1])) {
            return std::make_pair(doc_->cursor_col - 1, doc_->cursor_row);
            }

            if (doc_->cursor_col < line.size() && utils::IsBracketCharacter(line[doc_->cursor_col])) {
                return std::make_pair(doc_->cursor_col, doc_->cursor_row);
            }

            return std::nullopt;
    }

    std::optional<std::pair<size_t, size_t>> EditorComponent::FindMatchingBracket() const {
        static constexpr size_t kMaxBracketScanCharacters = 200000;

        const auto origin = FindBracketNearCursor();
        if (!origin || !doc_) {
            return std::nullopt;
        }

        const size_t origin_x = origin->first;
        const size_t origin_y = origin->second;
        if (origin_y >= doc_->lines.size() || origin_x >= doc_->lines[origin_y].size()) {
            return std::nullopt;
        }

        const char bracket = doc_->lines[origin_y][origin_x];
        const char match = utils::MatchingBracketFor(bracket);
        if (match == '\0') {
            return std::nullopt;
        }

        const auto ignored_brackets =
            BuildIgnoredBracketMask(doc_->lines, CurrentFilePath());
        if (origin_y < ignored_brackets.size() &&
            origin_x < ignored_brackets[origin_y].size() &&
            ignored_brackets[origin_y][origin_x]) {
            return std::nullopt;
        }

        size_t scanned_characters = 0;
        int balance = 0;

        if (utils::IsOpeningBracket(bracket)) {
            for (size_t y = origin_y; y < doc_->lines.size(); ++y) {
                const std::string& line = doc_->lines[y];
                const size_t start_x = y == origin_y ? origin_x : 0;
                for (size_t x = start_x; x < line.size(); ++x) {
                    if (++scanned_characters > kMaxBracketScanCharacters) return std::nullopt;
                    if (y < ignored_brackets.size() &&
                        x < ignored_brackets[y].size() &&
                        ignored_brackets[y][x]) {
                        continue;
                    }

                    const char current = line[x];
                    if (current == bracket) {
                        ++balance;
                    } else if (current == match) {
                        --balance;
                        if (balance == 0) return std::make_pair(x, y);
                    }
                }
            }
        } else if (utils::IsClosingBracket(bracket)) {
            for (size_t y = origin_y + 1; y > 0; --y) {
                const size_t line_index = y - 1;
                const std::string& line = doc_->lines[line_index];
                if (line.empty()) continue;

                size_t x = line_index == origin_y ? origin_x : line.size() - 1;
                while (true) {
                    if (++scanned_characters > kMaxBracketScanCharacters) return std::nullopt;
                    if (line_index < ignored_brackets.size() &&
                        x < ignored_brackets[line_index].size() &&
                        ignored_brackets[line_index][x]) {
                        if (x == 0) break;
                        --x;
                        continue;
                    }

                    const char current = line[x];
                    if (current == bracket) {
                        ++balance;
                    } else if (current == match) {
                        --balance;
                        if (balance == 0) return std::make_pair(x, line_index);
                    }

                    if (x == 0) break;
                    --x;
                }
            }
        }

        return std::nullopt;
    }

    bool EditorComponent::Focusable() const {
        return true;
    }

    size_t EditorComponent::LineNumberWidth() const {
        // Calculate width based on the document's total lines
        return doc_ ? std::to_string(doc_->lines.size()).size() : 1;
    }

    std::string EditorComponent::LineNumberText(size_t line_index, size_t width) const {
        std::string line_number = std::to_string(line_index + 1);
        if (line_number.size() < width) {
            line_number.insert(line_number.begin(), width - line_number.size(), ' ');
        }
        return line_number + " │ ";
    }

    bool EditorComponent::IsWordCharacter(char character) {
        return utils::IsWordCharacter(character);
    }

    bool EditorComponent::IsCharacterSelected(size_t x, size_t y) const {
        return doc_ && doc_->IsPositionSelected(x, y);
    }

    const EditorComponent::SearchMatch* EditorComponent::SearchMatchAt(size_t x, size_t y) const {
        for (const SearchMatch& match : search_matches_) {
            if (match.y == y && x >= match.x && x < match.x + match.length) {
                return &match;
            }
        }
        return nullptr;
    }

    bool EditorComponent::IsActiveSearchMatch(const SearchMatch& match) const {
        if (!search_matches_.empty() && current_search_match_ < search_matches_.size()) {
            const SearchMatch& active_match = search_matches_[current_search_match_];
            if (match.x == active_match.x &&
                match.y == active_match.y &&
                match.length == active_match.length) {
                return true;
                }
        }

        // Check if the match is currently being traversed by the cursor
        return doc_ && match.y == doc_->cursor_row &&
        doc_->cursor_col >= match.x &&
        doc_->cursor_col < match.x + match.length;
    }

    void EditorComponent::BeginSelection() {
        if (doc_) doc_->BeginSelection();
    }
