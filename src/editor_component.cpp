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
#include "text_transformer.hpp"

namespace textlt {

namespace {

bool IsPairOpeningCharacter(char character) {
    return character == '{' || character == '[' || character == '(' ||
        character == '"' || character == '\'';
}

bool IsPairClosingCharacter(char character) {
    return character == '}' || character == ']' || character == ')' ||
        character == '"' || character == '\'';
}

bool IsBlankLine(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](unsigned char character) {
        return std::isspace(character);
    });
}

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

std::string LeadingIndent(const std::string& line) {
    size_t end = 0;
    while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) {
        ++end;
    }
    return line.substr(0, end);
}

std::string TrimRightWhitespace(std::string value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

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

std::string ToLowerCopy(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}


} // namespace

EditorComponent::EditorComponent(EditorConfig* config, const Theme* theme)
    : config_(config),
      theme_(theme) {
    // Initialize with at least one empty line of text.
    text_lines_.push_back("");
    if (config_) {
        search_match_case_ = config_->search_match_case;
        search_whole_word_ = config_->search_whole_word;
    }
}

ftxui::Element EditorComponent::Render() {
    return RenderViewport();
}

void EditorComponent::SaveToFile(const std::string& path) {
    const std::string save_path = path.empty() ? current_filepath_ : path;
    std::ofstream file(save_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to save file: " + save_path);
    }

    const std::string line_ending =
        active_line_ending_ == LineEnding::CRLF ? "\r\n" : "\n";
    for (size_t i = 0; i < text_lines_.size(); ++i) {
        file << text_lines_[i];
        if (i + 1 < text_lines_.size()) {
            file << line_ending;
        }
    }

    current_filepath_ = save_path;
    is_dirty_ = false;
}

void EditorComponent::LoadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + path);
    }

    const std::string content{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};

    size_t lf_count = 0;
    size_t crlf_count = 0;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            ++lf_count;
            if (i > 0 && content[i - 1] == '\r') {
                ++crlf_count;
            }
        }
    }
    active_line_ending_ = (lf_count > 0 && crlf_count == lf_count)
        ? LineEnding::CRLF
        : LineEnding::LF;

    text_lines_.clear();
    std::string line;
    for (char character : content) {
        if (character == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            text_lines_.push_back(line);
            line.clear();
        } else {
            line.push_back(character);
        }
    }
    if (!content.empty() && content.back() != '\n') {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        text_lines_.push_back(line);
    }

    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    current_filepath_ = path;
    cursor_x_ = 0;
    cursor_y_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    history_manager_.Clear();
    is_dirty_ = false;
    ClearSearchHighlights();
    ClearSelection();
}

void EditorComponent::NewFile(const std::string& path) {
    text_lines_.clear();
    text_lines_.push_back("");
    current_filepath_ = path.empty() ? "Untitled" : path;
    active_line_ending_ = LineEnding::LF;
    cursor_x_ = 0;
    cursor_y_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    history_manager_.Clear();
    is_dirty_ = false;
    ClearSearchHighlights();
    ClearSelection();
}

const std::string& EditorComponent::CurrentFilePath() const {
    return current_filepath_;
}

bool EditorComponent::IsDirty() const {
    return is_dirty_;
}

EditorComponent::LineEnding EditorComponent::ActiveLineEnding() const {
    return active_line_ending_;
}

std::string EditorComponent::ActiveLineEndingLabel() const {
    return active_line_ending_ == LineEnding::CRLF ? "CRLF" : "LF";
}

int EditorComponent::GetCursorRow() const {
    return static_cast<int>(cursor_y_);
}

int EditorComponent::GetCursorCol() const {
    return static_cast<int>(cursor_x_);
}

size_t EditorComponent::GetLineCount() const {
    return text_lines_.size();
}

std::string EditorComponent::TextFromCursor() const {
    if (text_lines_.empty()) {
        return "";
    }

    const size_t row = std::min(cursor_y_, text_lines_.size() - 1);
    const size_t column = std::min(cursor_x_, text_lines_[row].size());
    std::string text = text_lines_[row].substr(column);
    for (size_t line_index = row + 1; line_index < text_lines_.size(); ++line_index) {
        text.push_back('\n');
        text += text_lines_[line_index];
    }
    return text;
}

std::string EditorComponent::GetAllText() const {
    if (text_lines_.empty()) {
        return "";
    }

    std::string full_text;
    full_text.reserve(text_lines_.size() * 80); // Estimate capacity for efficiency

    const std::string line_ending =
        active_line_ending_ == LineEnding::CRLF ? "\r\n" : "\n";

    for (size_t i = 0; i < text_lines_.size(); ++i) {
        full_text += text_lines_[i];
        if (i + 1 < text_lines_.size()) {
            full_text += line_ending;
        }
    }
    return full_text;
}

void EditorComponent::SetBottomOverlayRows(size_t rows) {
    bottom_overlay_rows_ = rows;
}

void EditorComponent::JumpToLine(int line_number) {
    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    if (line_number < 1) {
        line_number = 1;
    }
    const int max_line = static_cast<int>(text_lines_.size());
    if (line_number > max_line) {
        line_number = max_line;
    }

    EndTypingGroup();
    cursor_y_ = static_cast<size_t>(line_number - 1);
    cursor_x_ = 0;
    ClearSelection();
    UpdateScroll();
}

void EditorComponent::SetCursorPosition(size_t row, size_t column) {
    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    EndTypingGroup();
    cursor_y_ = std::min(row, text_lines_.size() - 1);
    cursor_x_ = std::min(column, text_lines_[cursor_y_].size());
    ClearSelection();
    UpdateScroll();
}

bool EditorComponent::HasSelection() const {
    return has_selection_ &&
        (cursor_x_ != selection_anchor_x_ || cursor_y_ != selection_anchor_y_);
}

void EditorComponent::SelectAll() {
    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    EndTypingGroup();
    selection_anchor_x_ = 0;
    selection_anchor_y_ = 0;
    cursor_y_ = text_lines_.size() - 1;
    cursor_x_ = text_lines_[cursor_y_].size();
    has_selection_ = true;
    UpdateScroll();
}

std::string EditorComponent::GetSelectedText() const {
    if (!HasSelection()) {
        return "";
    }

    auto [start, end] = utils::OrderedSelection(
        {selection_anchor_x_, selection_anchor_y_},
        {cursor_x_, cursor_y_},
        text_lines_);

    if (start.y == end.y) {
        return text_lines_[start.y].substr(start.x, end.x - start.x);
    }

    std::string selected = text_lines_[start.y].substr(start.x);
    selected.push_back('\n');
    for (size_t y = start.y + 1; y < end.y; ++y) {
        selected += text_lines_[y];
        selected.push_back('\n');
    }
    selected += text_lines_[end.y].substr(0, end.x);
    return selected;
}

void EditorComponent::DeleteSelection() {
    if (!HasSelection()) {
        return;
    }

    EndTypingGroup();
    SaveSnapshot();
    DeleteSelectionWithoutSnapshot();
}

void EditorComponent::DeleteSelectionWithoutSnapshot() {
    if (!HasSelection()) {
        return;
    }

    auto [start, end] = utils::OrderedSelection(
        {selection_anchor_x_, selection_anchor_y_},
        {cursor_x_, cursor_y_},
        text_lines_);

    if (start.y == end.y) {
        text_lines_[start.y].erase(start.x, end.x - start.x);
    } else {
        text_lines_[start.y] =
            text_lines_[start.y].substr(0, start.x) +
            text_lines_[end.y].substr(end.x);
        text_lines_.erase(
            text_lines_.begin() + static_cast<std::ptrdiff_t>(start.y + 1),
            text_lines_.begin() + static_cast<std::ptrdiff_t>(end.y + 1));
    }

    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    cursor_x_ = start.x;
    cursor_y_ = start.y;
    ClampCursorToBuffer();
    ClearSelection();
    UpdateScroll();
}

size_t EditorComponent::FindMatchAtOrAfterCursor() const {
    if (search_matches_.empty()) {
        return 0;
    }

    for (size_t i = 0; i < search_matches_.size(); ++i) {
        const SearchMatch& match = search_matches_[i];
        if (match.y > cursor_y_ ||
            (match.y == cursor_y_ && match.x + match.length > cursor_x_)) {
            return i;
        }
    }

    return 0;
}

void EditorComponent::MoveCursorToSearchMatch(const SearchMatch& match) {
    cursor_y_ = match.y;
    cursor_x_ = match.x;
    ClearSelection();
    UpdateScroll();
}

std::string EditorComponent::GetCommentPrefix() const {
    const std::string filename =
        std::filesystem::path(current_filepath_).filename().string();
    const std::string extension =
        std::filesystem::path(current_filepath_).extension().string();

    if (extension == ".sql" || extension == ".graphql" || extension == ".gql") {
        return "--";
    }

    if (filename.rfind("Dockerfile", 0) == 0 ||
        filename == ".bashrc" || filename == ".profile" ||
        filename == ".env" || filename == ".env.local" ||
        filename == ".env.development" || filename == ".env.production" ||
        (filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".env") == 0) ||
        extension == ".conf" || extension == ".ini" || extension == ".py" ||
        extension == ".rb" || extension == ".yaml" || extension == ".yml" ||
        extension == ".sh" || extension == ".bash" || extension == ".zsh" ||
        extension == ".bashrc" || extension == ".profile") {
        return "#";
    }

    return "//";
}

void EditorComponent::ToggleComment() {
    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    EndTypingGroup();
    ClampCursorToBuffer();
    SaveSnapshot();

    size_t start_row = cursor_y_;
    size_t end_row = cursor_y_;
    if (HasSelection()) {
        auto [start, end] = utils::OrderedSelection(
            {selection_anchor_x_, selection_anchor_y_},
            {cursor_x_, cursor_y_},
            text_lines_);
        start_row = start.y;
        end_row = end.y;
        if (end.x == 0 && end_row > start_row) {
            --end_row;
        }
    }

    const std::string prefix = GetCommentPrefix();
    for (size_t row = start_row; row <= end_row; ++row) {
        std::string& line = text_lines_[row];
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

    is_dirty_ = true;
    ClampCursorToBuffer();
    UpdateScroll();
}

void EditorComponent::ClearSelection() {
    ClampCursorToBuffer();
    has_selection_ = false;
    selection_anchor_x_ = cursor_x_;
    selection_anchor_y_ = cursor_y_;
}

void EditorComponent::InsertText(const std::string& text) {
    if (text.empty()) {
        return;
    }

    EndTypingGroup();
    SaveSnapshot();

    if (HasSelection()) {
        DeleteSelectionWithoutSnapshot();
    }

    for (char character : text) {
        if (character == '\r') {
            continue;
        }

        if (character == '\n') {
            std::string next_line = text_lines_[cursor_y_].substr(cursor_x_);
            text_lines_[cursor_y_].erase(cursor_x_);
            text_lines_.insert(text_lines_.begin() + cursor_y_ + 1, std::move(next_line));
            cursor_y_++;
            cursor_x_ = 0;
            continue;
        }

        text_lines_[cursor_y_].insert(cursor_x_, std::string(1, character));
        cursor_x_++;
    }

    is_dirty_ = true;
    ClearSelection();
    UpdateScroll();
}

bool EditorComponent::HandleAutoPairCharacter(const std::string& input) {
    if (input.size() != 1) {
        return false;
    }

    const char character = input.front();
    if (IsPairClosingCharacter(character) &&
        cursor_x_ < text_lines_[cursor_y_].size() &&
        text_lines_[cursor_y_][cursor_x_] == character) {
        EndTypingGroup();
        cursor_x_++;
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

    EndTypingGroup();
    SaveSnapshot();

    if (HasSelection()) {
        const std::string selected = GetSelectedText();
        DeleteSelectionWithoutSnapshot();
        text_lines_[cursor_y_].insert(cursor_x_, std::string(1, character) + selected + closing);
        cursor_x_ += selected.size() + 2;
    } else {
        text_lines_[cursor_y_].insert(cursor_x_, std::string(1, character) + closing);
        cursor_x_++;
    }

    is_dirty_ = true;
    ClearSelection();
    UpdateScroll();
    return true;
}

bool EditorComponent::HandleAutoIndentReturn() {
    EndTypingGroup();
    SaveSnapshot();
    if (HasSelection()) {
        DeleteSelectionWithoutSnapshot();
    }

    const std::string line_before_cursor = text_lines_[cursor_y_].substr(0, cursor_x_);
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

    std::string next_line = text_lines_[cursor_y_].substr(cursor_x_);
    text_lines_[cursor_y_].erase(cursor_x_);
    text_lines_.insert(text_lines_.begin() + cursor_y_ + 1, indent + next_line);
    cursor_y_++;
    cursor_x_ = indent.size();
    is_dirty_ = true;
    ClearSelection();
    UpdateScroll();
    return true;
}

void EditorComponent::ConvertTabsToSpaces() {
    const int configured_tab_size = config_ ? config_->tab_size : 4;
    const size_t tab_size = configured_tab_size == 2 ? 2 : 4;

    bool has_tabs = false;
    for (const std::string& line : text_lines_) {
        if (line.find('\t') != std::string::npos) {
            has_tabs = true;
            break;
        }
    }
    if (!has_tabs) {
        return;
    }

    EndTypingGroup();
    ClampCursorToBuffer();
    SaveSnapshot();

    size_t adjusted_cursor_x = cursor_x_;
    const std::string spaces(tab_size, ' ');
    for (size_t y = 0; y < text_lines_.size(); ++y) {
        std::string& line = text_lines_[y];
        if (y == cursor_y_) {
            size_t tabs_before_cursor = 0;
            const size_t cursor_limit = std::min(cursor_x_, line.size());
            for (size_t x = 0; x < cursor_limit; ++x) {
                if (line[x] == '\t') {
                    ++tabs_before_cursor;
                }
            }
            adjusted_cursor_x = cursor_x_ + tabs_before_cursor * (tab_size - 1);
        }

        size_t tab_position = line.find('\t');
        while (tab_position != std::string::npos) {
            line.replace(tab_position, 1, spaces);
            tab_position = line.find('\t', tab_position + tab_size);
        }
    }

    cursor_x_ = std::min(adjusted_cursor_x, text_lines_[cursor_y_].size());
    ClearSelection();
    is_dirty_ = true;
    UpdateScroll();
}

void EditorComponent::Convert4To2Spaces() {
    EndTypingGroup();
    ClampCursorToBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::Convert4To2Spaces(
        text_lines_,
        {cursor_x_, cursor_y_},
        {HasSelection(), selection_anchor_x_, selection_anchor_y_});
    if (!result.changed) {
        return;
    }

    history_manager_.PushSnapshot(before);
    cursor_x_ = result.cursor.x;
    cursor_y_ = result.cursor.y;
    selection_anchor_x_ = result.selection.anchor_x;
    selection_anchor_y_ = result.selection.anchor_y;
    has_selection_ = result.selection.active;
    is_dirty_ = true;
    ClampCursorToBuffer();
    UpdateScroll();
}

void EditorComponent::Convert2To4Spaces() {
    EndTypingGroup();
    ClampCursorToBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::Convert2To4Spaces(
        text_lines_,
        {cursor_x_, cursor_y_},
        {HasSelection(), selection_anchor_x_, selection_anchor_y_});
    if (!result.changed) {
        return;
    }

    history_manager_.PushSnapshot(before);
    cursor_x_ = result.cursor.x;
    cursor_y_ = result.cursor.y;
    selection_anchor_x_ = result.selection.anchor_x;
    selection_anchor_y_ = result.selection.anchor_y;
    has_selection_ = result.selection.active;
    is_dirty_ = true;
    ClampCursorToBuffer();
    UpdateScroll();
}

void EditorComponent::ToggleCase() {
    EndTypingGroup();
    ClampCursorToBuffer();
    const HistoryManager::State before = CurrentState();
    transform::TransformResult result = transform::ToggleCase(
        text_lines_,
        {cursor_x_, cursor_y_},
        {HasSelection(), selection_anchor_x_, selection_anchor_y_});
    if (!result.changed) {
        return;
    }

    history_manager_.PushSnapshot(before);
    cursor_x_ = result.cursor.x;
    cursor_y_ = result.cursor.y;
    selection_anchor_x_ = result.selection.anchor_x;
    selection_anchor_y_ = result.selection.anchor_y;
    has_selection_ = result.selection.active;
    is_dirty_ = true;
    UpdateScroll();
}

void EditorComponent::HighlightMatches(const std::string& query) {
    search_matches_.clear();
    current_search_match_ = 0;

    if (query.empty()) {
        return;
    }

    const std::string needle = search_match_case_ ? query : ToLowerCopy(query);
    for (size_t y = 0; y < text_lines_.size(); ++y) {
        const std::string& line = text_lines_[y];
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
    if (search_matches_.empty()) {
        return;
    }

    current_search_match_ = 0;
    for (size_t i = 0; i < search_matches_.size(); ++i) {
        const SearchMatch& match = search_matches_[i];
        if (match.y > cursor_y_ || (match.y == cursor_y_ && match.x > cursor_x_)) {
            current_search_match_ = i;
            break;
        }
    }
    MoveCursorToSearchMatch(search_matches_[current_search_match_]);
}

void EditorComponent::JumpToPreviousMatch() {
    if (search_matches_.empty()) {
        return;
    }

    current_search_match_ = search_matches_.size() - 1;
    for (size_t i = search_matches_.size(); i-- > 0;) {
        const SearchMatch& match = search_matches_[i];
        if (match.y < cursor_y_ || (match.y == cursor_y_ && match.x < cursor_x_)) {
            current_search_match_ = i;
            break;
        }
    }
    MoveCursorToSearchMatch(search_matches_[current_search_match_]);
}

void EditorComponent::ExecuteReplaceNext(
    const std::string& query,
    const std::string& replacement) {
    HighlightMatches(query);
    if (search_matches_.empty()) {
        return;
    }

    const SearchMatch match = search_matches_[current_search_match_];
    SaveSnapshot();
    text_lines_[match.y].replace(match.x, match.length, replacement);
    cursor_y_ = match.y;
    cursor_x_ = match.x + replacement.size();
    ClearSelection();
    HighlightMatches(query);
    UpdateScroll();
}

void EditorComponent::ExecuteReplaceAll(
    const std::string& query,
    const std::string& replacement) {
    HighlightMatches(query);
    if (search_matches_.empty()) {
        return;
    }

    SaveSnapshot();
    for (size_t index = search_matches_.size(); index-- > 0;) {
        const SearchMatch& match = search_matches_[index];
        text_lines_[match.y].replace(match.x, match.length, replacement);
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

std::optional<std::pair<int, int>> EditorComponent::FindBracketNearCursor() const {
    if (text_lines_.empty() || cursor_y_ >= text_lines_.size()) {
        return std::nullopt;
    }

    const std::string& line = text_lines_[cursor_y_];

    // Prefer the character to the left because the cursor commonly sits after
    // a just-typed bracket. The right-side check covers hovering before a pair.
    if (cursor_x_ > 0 && cursor_x_ - 1 < line.size() &&
        utils::IsBracketCharacter(line[cursor_x_ - 1])) {
        return std::make_pair(
            static_cast<int>(cursor_x_ - 1),
            static_cast<int>(cursor_y_));
    }

    if (cursor_x_ < line.size() && utils::IsBracketCharacter(line[cursor_x_])) {
        return std::make_pair(
            static_cast<int>(cursor_x_),
            static_cast<int>(cursor_y_));
    }

    return std::nullopt;
}

std::optional<std::pair<int, int>> EditorComponent::FindMatchingBracket() const {
    static constexpr size_t kMaxBracketScanCharacters = 200000;

    const auto origin = FindBracketNearCursor();
    if (!origin) {
        return std::nullopt;
    }

    const size_t origin_x = static_cast<size_t>(origin->first);
    const size_t origin_y = static_cast<size_t>(origin->second);
    if (origin_y >= text_lines_.size() || origin_x >= text_lines_[origin_y].size()) {
        return std::nullopt;
    }

    const char bracket = text_lines_[origin_y][origin_x];
    const char match = utils::MatchingBracketFor(bracket);
    if (match == '\0') {
        return std::nullopt;
    }

    size_t scanned_characters = 0;
    int balance = 0;

    if (utils::IsOpeningBracket(bracket)) {
        // Scan forward from the origin bracket, counting nested pairs of the
        // same type so the first balanced closing token is selected.
        for (size_t y = origin_y; y < text_lines_.size(); ++y) {
            const std::string& line = text_lines_[y];
            const size_t start_x = y == origin_y ? origin_x : 0;
            for (size_t x = start_x; x < line.size(); ++x) {
                if (++scanned_characters > kMaxBracketScanCharacters) {
                    return std::nullopt;
                }

                const char current = line[x];
                if (current == bracket) {
                    ++balance;
                } else if (current == match) {
                    --balance;
                    if (balance == 0) {
                        return std::make_pair(static_cast<int>(x), static_cast<int>(y));
                    }
                }
            }
        }
        return std::nullopt;
    }

    if (!utils::IsClosingBracket(bracket)) {
        return std::nullopt;
    }

    // Scan backward from the origin bracket using the same nesting counter in
    // reverse. This avoids matching a closing token to an inner opening token.
    for (size_t y = origin_y + 1; y > 0; --y) {
        const size_t line_index = y - 1;
        const std::string& line = text_lines_[line_index];
        if (line.empty()) {
            continue;
        }

        size_t x = line_index == origin_y ? origin_x : line.size() - 1;
        while (true) {
            if (++scanned_characters > kMaxBracketScanCharacters) {
                return std::nullopt;
            }

            const char current = line[x];
            if (current == bracket) {
                ++balance;
            } else if (current == match) {
                --balance;
                if (balance == 0) {
                    return std::make_pair(static_cast<int>(x), static_cast<int>(line_index));
                }
            }

            if (x == 0) {
                break;
            }
            --x;
        }
    }

    return std::nullopt;
}

bool EditorComponent::Focusable() const {
    return true;
}

size_t EditorComponent::LineNumberWidth() const {
    return std::to_string(text_lines_.size()).size();
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
    if (!HasSelection()) {
        return false;
    }

    auto [start, end] = utils::OrderedSelection(
        {selection_anchor_x_, selection_anchor_y_},
        {cursor_x_, cursor_y_},
        text_lines_);
    const utils::Position character_position{x, y};
    return !utils::PositionLess(character_position, start) &&
        utils::PositionLess(character_position, end);
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

    return match.y == cursor_y_ &&
        cursor_x_ >= match.x &&
        cursor_x_ < match.x + match.length;
}

void EditorComponent::BeginSelection() {
    if (!has_selection_) {
        selection_anchor_x_ = cursor_x_;
        selection_anchor_y_ = cursor_y_;
    }
    has_selection_ = true;
}

void EditorComponent::ClampCursorToBuffer() {
    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    cursor_y_ = std::min(cursor_y_, text_lines_.size() - 1);
    cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
}

void EditorComponent::MoveCursorHome() {
    cursor_x_ = 0;
}

void EditorComponent::MoveCursorEnd() {
    cursor_x_ = text_lines_[cursor_y_].size();
}

void EditorComponent::MoveCursorLeft() {
    if (cursor_x_ > 0) {
        cursor_x_--;
    } else if (cursor_y_ > 0) {
        cursor_y_--;
        cursor_x_ = text_lines_[cursor_y_].size();
    }
}

void EditorComponent::MoveCursorRight() {
    if (cursor_x_ < text_lines_[cursor_y_].size()) {
        cursor_x_++;
    } else if (cursor_y_ + 1 < text_lines_.size()) {
        cursor_y_++;
        cursor_x_ = 0;
    }
}

void EditorComponent::MoveCursorUp() {
    ClampCursorToBuffer();
    if (cursor_y_ > 0) {
        cursor_y_--;
    }
    cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
}

void EditorComponent::MoveCursorDown() {
    ClampCursorToBuffer();
    if (cursor_y_ < text_lines_.size() - 1) {
        cursor_y_++;
    }
    cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
}

void EditorComponent::MoveCursorPageUp() {
    ClampCursorToBuffer();
    const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
    cursor_y_ = cursor_y_ > page_step ? cursor_y_ - page_step : 0;
    cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
}

void EditorComponent::MoveCursorPageDown() {
    ClampCursorToBuffer();
    const size_t page_step = std::max<size_t>(1, VisibleHeight() - 1);
    cursor_y_ = std::min(cursor_y_ + page_step, text_lines_.size() - 1);
    cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
}

void EditorComponent::MoveCursorToPreviousParagraph() {
    ClampCursorToBuffer();

    if (cursor_y_ == 0) {
        cursor_x_ = 0;
        return;
    }

    if (!IsBlankLine(text_lines_[cursor_y_])) {
        size_t paragraph_start = cursor_y_;
        while (paragraph_start > 0 && !IsBlankLine(text_lines_[paragraph_start - 1])) {
            --paragraph_start;
        }
        if (cursor_y_ != paragraph_start || cursor_x_ != 0) {
            cursor_y_ = paragraph_start;
            cursor_x_ = 0;
            return;
        }
    }

    size_t target = cursor_y_;
    if (target > 0) {
        --target;
    }
    while (target > 0 && IsBlankLine(text_lines_[target])) {
        --target;
    }
    while (target > 0 && !IsBlankLine(text_lines_[target - 1])) {
        --target;
    }

    cursor_y_ = target;
    cursor_x_ = 0;
}

void EditorComponent::MoveCursorToNextParagraph() {
    ClampCursorToBuffer();

    size_t target = cursor_y_;
    if (!IsBlankLine(text_lines_[target])) {
        while (target + 1 < text_lines_.size() && !IsBlankLine(text_lines_[target + 1])) {
            ++target;
        }
    }
    while (target + 1 < text_lines_.size() && IsBlankLine(text_lines_[target + 1])) {
        ++target;
    }

    if (target + 1 < text_lines_.size()) {
        cursor_y_ = target + 1;
        cursor_x_ = 0;
        return;
    }

    cursor_y_ = text_lines_.size() - 1;
    cursor_x_ = text_lines_[cursor_y_].size();
}

void EditorComponent::MoveCursorToPreviousWord() {
    ClampCursorToBuffer();

    if (cursor_x_ == 0) {
        if (cursor_y_ == 0) {
            return;
        }
        cursor_y_--;
        cursor_x_ = text_lines_[cursor_y_].size();
    }

    const std::string& line = text_lines_[cursor_y_];
    while (cursor_x_ > 0 && !IsWordCharacter(line[cursor_x_ - 1])) {
        cursor_x_--;
    }
    while (cursor_x_ > 0 && IsWordCharacter(line[cursor_x_ - 1])) {
        cursor_x_--;
    }

    ClampCursorToBuffer();
}

void EditorComponent::MoveCursorToNextWord() {
    ClampCursorToBuffer();

    const std::string& line = text_lines_[cursor_y_];
    while (cursor_x_ < line.size() && IsWordCharacter(line[cursor_x_])) {
        cursor_x_++;
    }
    while (cursor_x_ < line.size() && !IsWordCharacter(line[cursor_x_])) {
        cursor_x_++;
    }

    if (cursor_x_ == line.size() && cursor_y_ + 1 < text_lines_.size()) {
        cursor_y_++;
        cursor_x_ = 0;
    }

    ClampCursorToBuffer();
}

void EditorComponent::DeleteWordBackward() {
    EndTypingGroup();
    ClampCursorToBuffer();
    if (HasSelection()) {
        DeleteSelection();
        return;
    }

    if (cursor_x_ == 0) {
        if (cursor_y_ == 0) {
            return;
        }

        SaveSnapshot();
        cursor_x_ = text_lines_[cursor_y_ - 1].size();
        text_lines_[cursor_y_ - 1] += text_lines_[cursor_y_];
        text_lines_.erase(text_lines_.begin() + static_cast<std::ptrdiff_t>(cursor_y_));
        cursor_y_--;
        is_dirty_ = true;
        ClearSelection();
        UpdateScroll();
        return;
    }

    const size_t target = utils::FindWordDeleteStart(text_lines_[cursor_y_], cursor_x_);
    if (target == cursor_x_) {
        return;
    }

    SaveSnapshot();
    text_lines_[cursor_y_].erase(target, cursor_x_ - target);
    cursor_x_ = target;
    is_dirty_ = true;
    ClearSelection();
    UpdateScroll();
}

void EditorComponent::DeleteWordForward() {
    EndTypingGroup();
    ClampCursorToBuffer();
    if (HasSelection()) {
        DeleteSelection();
        return;
    }

    if (cursor_x_ >= text_lines_[cursor_y_].size()) {
        if (cursor_y_ + 1 >= text_lines_.size()) {
            return;
        }

        SaveSnapshot();
        text_lines_[cursor_y_] += text_lines_[cursor_y_ + 1];
        text_lines_.erase(
            text_lines_.begin() + static_cast<std::ptrdiff_t>(cursor_y_ + 1));
        is_dirty_ = true;
        ClearSelection();
        UpdateScroll();
        return;
    }

    const size_t target = utils::FindWordDeleteEnd(text_lines_[cursor_y_], cursor_x_);
    if (target == cursor_x_) {
        return;
    }

    SaveSnapshot();
    text_lines_[cursor_y_].erase(cursor_x_, target - cursor_x_);
    is_dirty_ = true;
    ClearSelection();
    UpdateScroll();
}

bool EditorComponent::MoveLinesUp() {
    EndTypingGroup();
    ClampCursorToBuffer();

    if (text_lines_.size() < 2) {
        return false;
    }

    if (!HasSelection()) {
        if (cursor_y_ == 0) {
            return false;
        }

        SaveSnapshot();
        std::swap(text_lines_[cursor_y_], text_lines_[cursor_y_ - 1]);
        --cursor_y_;
        cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
        ClearSelection();
        is_dirty_ = true;
        UpdateScroll();
        return true;
    }

    auto [start, end] = utils::OrderedSelection(
        {selection_anchor_x_, selection_anchor_y_},
        {cursor_x_, cursor_y_},
        text_lines_);
    size_t start_row = start.y;
    size_t end_row = end.y;
    if (end.x == 0 && end_row > start_row) {
        --end_row;
    }
    if (start_row == 0) {
        return false;
    }

    SaveSnapshot();
    std::rotate(
        text_lines_.begin() + static_cast<std::ptrdiff_t>(start_row - 1),
        text_lines_.begin() + static_cast<std::ptrdiff_t>(start_row),
        text_lines_.begin() + static_cast<std::ptrdiff_t>(end_row + 1));
    --cursor_y_;
    --selection_anchor_y_;
    ClampCursorToBuffer();
    selection_anchor_y_ = std::min(selection_anchor_y_, text_lines_.size() - 1);
    selection_anchor_x_ = std::min(selection_anchor_x_, text_lines_[selection_anchor_y_].size());
    has_selection_ = true;
    is_dirty_ = true;
    UpdateScroll();
    return true;
}

bool EditorComponent::MoveLinesDown() {
    EndTypingGroup();
    ClampCursorToBuffer();

    if (text_lines_.size() < 2) {
        return false;
    }

    if (!HasSelection()) {
        if (cursor_y_ + 1 >= text_lines_.size()) {
            return false;
        }

        SaveSnapshot();
        std::swap(text_lines_[cursor_y_], text_lines_[cursor_y_ + 1]);
        ++cursor_y_;
        cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
        ClearSelection();
        is_dirty_ = true;
        UpdateScroll();
        return true;
    }

    auto [start, end] = utils::OrderedSelection(
        {selection_anchor_x_, selection_anchor_y_},
        {cursor_x_, cursor_y_},
        text_lines_);
    size_t start_row = start.y;
    size_t end_row = end.y;
    if (end.x == 0 && end_row > start_row) {
        --end_row;
    }
    if (end_row + 1 >= text_lines_.size()) {
        return false;
    }

    SaveSnapshot();
    std::rotate(
        text_lines_.begin() + static_cast<std::ptrdiff_t>(start_row),
        text_lines_.begin() + static_cast<std::ptrdiff_t>(end_row + 1),
        text_lines_.begin() + static_cast<std::ptrdiff_t>(end_row + 2));
    ++cursor_y_;
    ++selection_anchor_y_;
    ClampCursorToBuffer();
    selection_anchor_y_ = std::min(selection_anchor_y_, text_lines_.size() - 1);
    selection_anchor_x_ = std::min(selection_anchor_x_, text_lines_[selection_anchor_y_].size());
    has_selection_ = true;
    is_dirty_ = true;
    UpdateScroll();
    return true;
}

bool EditorComponent::DuplicateLines() {
    EndTypingGroup();
    ClampCursorToBuffer();

    size_t start_row = cursor_y_;
    size_t end_row = cursor_y_;
    const bool had_selection = HasSelection();
    if (had_selection) {
        auto [start, end] = utils::OrderedSelection(
            {selection_anchor_x_, selection_anchor_y_},
            {cursor_x_, cursor_y_},
            text_lines_);
        start_row = start.y;
        end_row = end.y;
        if (end.x == 0 && end_row > start_row) {
            --end_row;
        }
    }

    if (start_row >= text_lines_.size() || end_row >= text_lines_.size() || start_row > end_row) {
        return false;
    }

    SaveSnapshot();
    const auto first = text_lines_.begin() + static_cast<std::ptrdiff_t>(start_row);
    const auto last = text_lines_.begin() + static_cast<std::ptrdiff_t>(end_row + 1);
    std::vector<std::string> copied_lines(first, last);
    text_lines_.insert(
        text_lines_.begin() + static_cast<std::ptrdiff_t>(end_row + 1),
        copied_lines.begin(),
        copied_lines.end());

    if (!had_selection) {
        ++cursor_y_;
        cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
        ClearSelection();
    } else {
        // Keep the original selection stable while inserting the duplicated block below it.
        has_selection_ = true;
    }

    is_dirty_ = true;
    UpdateScroll();
    return true;
}

HistoryManager::State EditorComponent::CurrentState() const {
    return {
        text_lines_,
        static_cast<int>(cursor_x_),
        static_cast<int>(cursor_y_),
    };
}

void EditorComponent::ApplyState(const HistoryManager::State& state) {
    text_lines_ = state.lines;
    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    cursor_x_ = state.cursor_x < 0 ? 0 : static_cast<size_t>(state.cursor_x);
    cursor_y_ = state.cursor_y < 0 ? 0 : static_cast<size_t>(state.cursor_y);
    ClearSelection();
    UpdateScroll();
}

void EditorComponent::SaveSnapshot() {
    ClampCursorToBuffer();
    history_manager_.PushSnapshot(CurrentState());
}

void EditorComponent::SaveSnapshotForTyping(const std::string& input) {
    ClampCursorToBuffer();
    history_manager_.PushSnapshotForTyping(input, CurrentState(), HasSelection());
}

void EditorComponent::EndTypingGroup() {
    history_manager_.EndTypingGroup();
}

void EditorComponent::Undo() {
    HistoryManager::State state;
    if (history_manager_.Undo(CurrentState(), &state)) {
        ApplyState(state);
    }
}

void EditorComponent::Redo() {
    HistoryManager::State state;
    if (history_manager_.Redo(CurrentState(), &state)) {
        ApplyState(state);
    }
}

std::string EditorComponent::GetCurrentLineText() const {
    if (cursor_y_ < text_lines_.size()) {
        return text_lines_[cursor_y_];
    }
    return "";
}

void EditorComponent::DeleteCurrentLine() {
    if (text_lines_.empty()) return;

    if (cursor_y_ < text_lines_.size()) {
        EndTypingGroup();
        SaveSnapshot();
        text_lines_.erase(text_lines_.begin() + cursor_y_);
        
        // Ensure the layout never drops to an absolute 0 rows state
        if (text_lines_.empty()) {
            text_lines_.push_back("");
            cursor_y_ = 0;
        } else if (cursor_y_ >= text_lines_.size()) {
            // If the deleted row was the last one, snap index to the new tail
            cursor_y_ = text_lines_.size() - 1;
        }
        
        // Reset horizontal position safely to the front of the line
        cursor_x_ = 0;
        ClearSelection();
        
        // Trigger layout adjustments so window scrolling does not break
        UpdateScroll();
    }
}

} // namespace textlt
