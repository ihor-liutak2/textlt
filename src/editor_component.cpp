#include "editor_component.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "editor_utils.hpp"
#include "syntax_highlighter.hpp"
#include "text_transformer.hpp"

namespace textlt {

    namespace {

        // Checks if the character is an opening bracket or quote
        bool IsPairOpeningCharacter(char character) {
            return character == '{' || character == '[' || character == '(' ||
            character == '"' || character == '\'';
        }

        // Checks if the character is a closing bracket or quote
        bool IsPairClosingCharacter(char character) {
            return character == '}' || character == ']' || character == ')' ||
            character == '"' || character == '\'';
        }

        // Returns true if the string contains only whitespace characters
        bool IsBlankLine(const std::string& line) {
            return std::all_of(line.begin(), line.end(), [](unsigned char character) {
                return std::isspace(character);
            });
        }

        // Returns the corresponding closing character for a given opening character
        char ClosingPairFor(char character) {
            switch (character) {
                case '{': return '}';
                case '[': return ']';
                case '(': return ')';
                case '"': return '"';
                case '\'': return '\'';
                default: return '\0';
            }
        }

        // Returns the leading whitespace (indentation) of a line
        std::string LeadingIndent(const std::string& line) {
            size_t end = 0;
            while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) {
                ++end;
            }
            return line.substr(0, end);
        }

        // Returns a copy of the string with trailing whitespace removed
        std::string TrimRightWhitespace(std::string value) {
            while (!value.empty() &&
                std::isspace(static_cast<unsigned char>(value.back()))) {
                value.pop_back();
                }
                return value;
        }

        // Checks if a trimmed line ends with the Ruby 'do' block keyword
        bool EndsWithRubyDo(const std::string& trimmed_line) {
            if (trimmed_line.size() < 2 ||
                trimmed_line.compare(trimmed_line.size() - 2, 2, "do") != 0) {
                return false;
                }
                if (trimmed_line.size() == 2) {
                    return true;
                }
                const unsigned char previous =
                static_cast<unsigned char>(trimmed_line[trimmed_line.size() - 3]);
            return !std::isalnum(previous) && previous != '_';
        }

        // Returns a lowercase copy of the provided string
        std::string ToLowerCopy(std::string value) {
            for (char& character : value) {
                character = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            }
            return value;
        }

        bool IsBracketIgnoredStyle(SyntaxHighlighter::Style style) {
            return style == SyntaxHighlighter::Style::String ||
                style == SyntaxHighlighter::Style::Comment;
        }

        std::vector<std::vector<bool>> BuildIgnoredBracketMask(
            const std::vector<std::string>& lines,
            const std::string& file_path) {
            std::vector<std::vector<bool>> ignored(lines.size());
            SyntaxHighlighter::TokenizationContext context;

            for (size_t y = 0; y < lines.size(); ++y) {
                ignored[y].assign(lines[y].size(), false);
                const auto tokens = SyntaxHighlighter::TokenizeLine(lines[y], file_path, &context);
                for (const SyntaxHighlighter::Token& token : tokens) {
                    if (!IsBracketIgnoredStyle(token.style)) {
                        continue;
                    }
                    const size_t end = std::min(lines[y].size(), token.start + token.length);
                    for (size_t x = token.start; x < end; ++x) {
                        ignored[y][x] = true;
                    }
                }
            }

            return ignored;
        }

    } // namespace
    
    /**
     * @brief Sets the active document for the editor.
     * * Switches the editor context to a new document, synchronizes UI state,
     * and ensures cursor position is clamped to the new content boundaries.
     */
    void EditorComponent::SetDocument(std::shared_ptr<Document> doc) {
        // Check if the provided document is valid to prevent null pointer dereference
        if (!doc) {
            return;
        }

        // Update the local document reference
        doc_ = doc;

        // Ensure cursor is within buffer limits to prevent out-of-bounds access
        ClampCursorToBuffer();

        // Refresh the viewport and scroll position to match the new document content
        UpdateScroll();

        // Clear previous search highlights and selections to avoid stale UI states
        ClearSearchHighlights();
        ClearSelection();
    }

    std::shared_ptr<Document> EditorComponent::GetDocument() const { return doc_; }

    EditorComponent::EditorComponent(EditorConfig* config, const Theme* theme)
    : config_(config),
    theme_(theme),
    doc_(std::make_shared<Document>()) { // Initialize default document

        if (config_) {
            search_match_case_ = config_->search_match_case;
            search_whole_word_ = config_->search_whole_word;
        }
    }

    ftxui::Element EditorComponent::Render() {
        return RenderViewport();
    }

    void EditorComponent::SaveToFile(const std::string& path) {
        if (!doc_) return;

        const std::string save_path = path.empty() ? doc_->path.string() : path;
        std::ofstream file(save_path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to save file: " + save_path);
        }

        file << doc_->ToContent();

        doc_->SetPath(save_path);
        doc_->is_dirty = false;
    }

    void EditorComponent::LoadFromFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to open file: " + path);
        }

        const std::string content{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};

        if (!doc_) doc_ = std::make_shared<Document>();
        doc_->LoadContent(content, path);
        scroll_x_ = 0;
        scroll_y_ = 0;
        ClearSearchHighlights();
        ClearSelection();
    }

    void EditorComponent::NewFile(const std::string& path) {
        if (!doc_) doc_ = std::make_shared<Document>();

        doc_->Reset();
        doc_->SetPath(path.empty() ? "Untitled" : path);

        scroll_x_ = 0;
        scroll_y_ = 0;
        ClearSearchHighlights();
        ClearSelection();
    }

    std::string EditorComponent::CurrentFilePath() const {
        return doc_ ? doc_->CurrentFilePath() : "Untitled";
    }

    bool EditorComponent::IsDirty() const {
        return doc_ ? doc_->is_dirty : false;
    }

    LineEnding EditorComponent::ActiveLineEnding() const {
        return doc_ ? doc_->line_ending : LineEnding::LF;
    }

    std::string EditorComponent::ActiveLineEndingLabel() const {
        return doc_ ? doc_->LineEndingLabel() : "LF";
    }

    size_t EditorComponent::GetCursorRow() const {
        return doc_ ? doc_->cursor_row : 0;
    }

    size_t EditorComponent::GetCursorCol() const {
        return doc_ ? doc_->cursor_col : 0;
    }

    size_t EditorComponent::GetLineCount() const {
        return doc_ ? doc_->LineCount() : 0;
    }

    std::string EditorComponent::TextFromCursor() const {
        return doc_ ? doc_->TextFromCursor() : "";
    }

    std::string EditorComponent::GetAllText() const {
        return doc_ ? doc_->ToContent() : "";
    }

    void EditorComponent::SetBottomOverlayRows(size_t rows) {
        bottom_overlay_rows_ = rows;
    }

    void EditorComponent::JumpToLine(size_t line_number) {
        if (!doc_) return;

        EndTypingGroup();
        doc_->JumpToLine(line_number);
        ClearSelection();
        UpdateScroll();
    }

    void EditorComponent::SetCursorPosition(size_t row, size_t column) {
        if (!doc_) return;

        EndTypingGroup();
        doc_->SetCursorPosition(row, column);
        ClearSelection();
        UpdateScroll();
    }

    bool EditorComponent::HasSelection() const {
        return doc_ && doc_->HasSelection();
    }

    void EditorComponent::SelectAll() {
        if (!doc_) return;
        EndTypingGroup();
        doc_->SelectAll();
        UpdateScroll();
    }

    std::string EditorComponent::GetSelectedText() const {
        return doc_ ? doc_->GetSelectedText() : "";
    }

    void EditorComponent::DeleteSelection() {
        if (doc_ && doc_->DeleteSelection()) {
            UpdateScroll();
        }
    }

    void EditorComponent::DeleteSelectionWithoutSnapshot() {
        if (doc_ && doc_->DeleteSelectionWithoutSnapshot()) {
            UpdateScroll();
        }
    }

    size_t EditorComponent::FindMatchAtOrAfterCursor() const {
        if (search_matches_.empty() || !doc_) {
            return 0;
        }

        for (size_t i = 0; i < search_matches_.size(); ++i) {
            const SearchMatch& match = search_matches_[i];
            if (match.y > doc_->cursor_row ||
                (match.y == doc_->cursor_row && match.x + match.length > doc_->cursor_col)) {
                return i;
                }
        }

        return 0;
    }

    void EditorComponent::MoveCursorToSearchMatch(const SearchMatch& match) {
        if (!doc_) return;
        doc_->cursor_row = match.y;
        doc_->cursor_col = match.x;
        ClearSelection();
        UpdateScroll();
    }

    std::string EditorComponent::GetCommentPrefix() const {
        return doc_ ? doc_->CommentPrefix() : "//";
    }

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

    void EditorComponent::InsertText(const std::string& text) {
        if (text.empty() || !doc_) {
            return;
        }

        EndTypingGroup();
        if (doc_->InsertText(text)) {
            ClearSelection();
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
        if (doc_) doc_->MoveCursorUp();
    }

    void EditorComponent::MoveCursorDown() {
        if (doc_) doc_->MoveCursorDown();
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

} // namespace textlt
