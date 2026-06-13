#include "editor_component.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "ftxui/component/event.hpp"

namespace textlt {

namespace {

enum class NavigationAction {
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    WordLeft,
    WordRight,
};

struct Position {
    size_t x = 0;
    size_t y = 0;
};

bool PositionLess(const Position& left, const Position& right) {
    return left.y < right.y || (left.y == right.y && left.x < right.x);
}

Position ClampedPosition(Position position, const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return {};
    }

    position.y = std::min(position.y, lines.size() - 1);
    position.x = std::min(position.x, lines[position.y].size());
    return position;
}

std::pair<Position, Position> OrderedSelection(Position anchor,
                                               Position cursor,
                                               const std::vector<std::string>& lines) {
    anchor = ClampedPosition(anchor, lines);
    cursor = ClampedPosition(cursor, lines);
    if (PositionLess(cursor, anchor)) {
        std::swap(anchor, cursor);
    }
    return {anchor, cursor};
}

bool IsShiftNavigationEvent(const ftxui::Event& event, NavigationAction* action) {
    const std::string& input = event.input();

    if (input == "\x1B[1;2D") {
        *action = NavigationAction::Left;
        return true;
    }
    if (input == "\x1B[1;2C") {
        *action = NavigationAction::Right;
        return true;
    }
    if (input == "\x1B[1;6D") {
        *action = NavigationAction::WordLeft;
        return true;
    }
    if (input == "\x1B[1;6C") {
        *action = NavigationAction::WordRight;
        return true;
    }
    if (input == "\x1B[1;2A") {
        *action = NavigationAction::Up;
        return true;
    }
    if (input == "\x1B[1;2B") {
        *action = NavigationAction::Down;
        return true;
    }
    if (input == "\x1B[1;2H" || input == "\x1B[7;2~") {
        *action = NavigationAction::Home;
        return true;
    }
    if (input == "\x1B[1;2F" || input == "\x1B[8;2~") {
        *action = NavigationAction::End;
        return true;
    }

    return false;
}

bool IsNavigationEvent(const ftxui::Event& event, NavigationAction* action) {
    if (event == ftxui::Event::ArrowLeft) {
        *action = NavigationAction::Left;
        return true;
    }
    if (event == ftxui::Event::ArrowRight) {
        *action = NavigationAction::Right;
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        *action = NavigationAction::Up;
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        *action = NavigationAction::Down;
        return true;
    }
    if (event == ftxui::Event::Home) {
        *action = NavigationAction::Home;
        return true;
    }
    if (event == ftxui::Event::End) {
        *action = NavigationAction::End;
        return true;
    }
    if (event == ftxui::Event::ArrowLeftCtrl) {
        *action = NavigationAction::WordLeft;
        return true;
    }
    if (event == ftxui::Event::ArrowRightCtrl) {
        *action = NavigationAction::WordRight;
        return true;
    }

    return false;
}

} // namespace

EditorComponent::EditorComponent(EditorConfig* config, const Theme* theme)
    : config_(config),
      theme_(theme) {
    // Initialize with at least one empty line of text.
    text_lines_.push_back("");
}

ftxui::Element EditorComponent::Render() {
    UpdateScroll();

    ftxui::Elements lines_elements;
    const size_t visible_height = VisibleHeight();
    const size_t last_visible_line =
        std::min(text_lines_.size(), scroll_y_ + visible_height);
    const size_t line_number_width = LineNumberWidth();
    const bool show_line_numbers = config_ && config_->show_line_numbers;
    const bool smart_word_wrap = config_ && config_->smart_word_wrap;
    const Theme& theme = theme_ ? *theme_ : FallbackTheme();
    const size_t line_number_columns =
        show_line_numbers ? LineNumberText(0, line_number_width).size() : 0;
    const size_t editor_columns = editor_box_.x_max >= editor_box_.x_min
        ? static_cast<size_t>(editor_box_.x_max - editor_box_.x_min + 1)
        : 80;
    const size_t wrap_width = std::max<size_t>(
        1,
        editor_columns > line_number_columns ? editor_columns - line_number_columns : editor_columns);

    auto wrapped_segments = [&](const std::string& line_content) {
        std::vector<std::pair<size_t, size_t>> segments;
        if (!smart_word_wrap || line_content.size() <= wrap_width) {
            segments.push_back({0, line_content.size()});
            return segments;
        }

        size_t start = 0;
        while (start < line_content.size()) {
            const size_t remaining = line_content.size() - start;
            if (remaining <= wrap_width) {
                segments.push_back({start, line_content.size()});
                break;
            }

            const size_t boundary = start + wrap_width;
            const size_t lower_bound = boundary > start + 10 ? boundary - 10 : start;
            size_t end = boundary;
            for (size_t pos = boundary; pos-- > lower_bound;) {
                if (line_content[pos] == ' ') {
                    end = pos + 1;
                    break;
                }
            }

            if (end <= start) {
                end = boundary;
            }
            segments.push_back({start, end});
            start = end;
        }

        return segments;
    };

    auto render_segment = [&](size_t line_index,
                              size_t segment_start,
                              size_t segment_end,
                              bool show_line_number) {
        const std::string& line_content = text_lines_[line_index];

        ftxui::Element line_number = show_line_numbers
            ? ftxui::text(show_line_number
                  ? LineNumberText(line_index, line_number_width)
                  : std::string(line_number_columns, ' ')) |
                ftxui::color(theme.gutter)
            : ftxui::text("");

        ftxui::Elements line_parts{line_number};
        const size_t cursor_x = line_index == cursor_y_
            ? std::min(cursor_x_, line_content.size())
            : line_content.size() + 1;
        for (size_t x = segment_start; x <= segment_end; ++x) {
            if (line_index == cursor_y_ && x == cursor_x) {
                line_parts.push_back(
                    ftxui::text("█") | ftxui::blink | ftxui::color(theme.cursor));
            }

            if (x == segment_end || x == line_content.size()) {
                continue;
            }

            ftxui::Element character = ftxui::text(line_content.substr(x, 1));
            const SearchMatch* search_match = SearchMatchAt(x, line_index);
            if (IsCharacterSelected(x, line_index)) {
                character = character |
                    ftxui::bgcolor(theme.selection_bg) |
                    ftxui::color(theme.selection_fg);
            } else if (search_match) {
                const ftxui::Color match_bg =
                    IsActiveSearchMatch(*search_match)
                        ? theme.active_match_bg
                        : theme.match_bg;
                character = character |
                    ftxui::bgcolor(match_bg) |
                    ftxui::color(theme.selection_fg);
            } else {
                character = character | ftxui::color(theme.editor_text);
            }
            line_parts.push_back(std::move(character));
        }

        return ftxui::hbox(std::move(line_parts));
    };

    // Render only the visible part of the buffer.
    for (size_t i = scroll_y_; i < last_visible_line; ++i) {
        const std::string& line_content = text_lines_[i];
        const auto segments = wrapped_segments(line_content);
        for (size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
            if (lines_elements.size() >= visible_height) {
                break;
            }
            lines_elements.push_back(render_segment(
                i,
                segments[segment_index].first,
                segments[segment_index].second,
                segment_index == 0));
        }
        if (lines_elements.size() >= visible_height) {
            break;
        }
    }

    while (lines_elements.size() < visible_height) {
        lines_elements.push_back(ftxui::hbox({
            show_line_numbers
                ? ftxui::text(std::string(line_number_width, ' ') + " │ ") |
                    ftxui::color(theme.gutter)
                : ftxui::text(""),
            ftxui::filler(),
        }));
    }

    return ftxui::vbox(std::move(lines_elements)) | ftxui::reflect(editor_box_);
}

void EditorComponent::SaveToFile(const std::string& path) {
    const std::string save_path = path.empty() ? current_filepath_ : path;
    std::ofstream file(save_path);
    if (!file) {
        throw std::runtime_error("Unable to save file: " + save_path);
    }

    for (size_t i = 0; i < text_lines_.size(); ++i) {
        file << text_lines_[i];
        if (i + 1 < text_lines_.size()) {
            file << '\n';
        }
    }

    current_filepath_ = save_path;
}

void EditorComponent::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + path);
    }

    text_lines_.clear();
    std::string line;
    while (std::getline(file, line)) {
        text_lines_.push_back(line);
    }

    if (text_lines_.empty()) {
        text_lines_.push_back("");
    }

    current_filepath_ = path;
    cursor_x_ = 0;
    cursor_y_ = 0;
    scroll_y_ = 0;
    undo_stack_.clear();
    redo_stack_.clear();
    typing_group_active_ = false;
    ClearSearchHighlights();
    ClearSelection();
}

const std::string& EditorComponent::CurrentFilePath() const {
    return current_filepath_;
}

bool EditorComponent::HasSelection() const {
    return has_selection_ &&
        (cursor_x_ != selection_anchor_x_ || cursor_y_ != selection_anchor_y_);
}

std::string EditorComponent::GetSelectedText() const {
    if (!HasSelection()) {
        return "";
    }

    auto [start, end] = OrderedSelection(
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

    auto [start, end] = OrderedSelection(
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

    ClearSelection();
    UpdateScroll();
}

void EditorComponent::HighlightMatches(const std::string& query) {
    search_matches_.clear();
    current_search_match_ = 0;

    if (query.empty()) {
        return;
    }

    for (size_t y = 0; y < text_lines_.size(); ++y) {
        const std::string& line = text_lines_[y];
        size_t position = line.find(query);
        while (position != std::string::npos) {
            search_matches_.push_back({position, y, query.size()});
            position = line.find(query, position + query.size());
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
    for (std::string& line : text_lines_) {
        size_t position = line.find(query);
        while (position != std::string::npos) {
            line.replace(position, query.size(), replacement);
            position = line.find(query, position + replacement.size());
        }
    }

    ClampCursorToBuffer();
    ClearSelection();
    HighlightMatches(query);
    UpdateScroll();
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

bool EditorComponent::OnEvent(ftxui::Event event) {
    if (event.is_mouse()) {
        auto mouse = event.mouse();
        
        bool inside_editor = (mouse.x >= editor_box_.x_min && mouse.x <= editor_box_.x_max &&
                              mouse.y >= editor_box_.y_min && mouse.y <= editor_box_.y_max);
        
        if (inside_editor) {
            // 1. Reclaim focus instantly when the user physically clicks inside the editor area
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                TakeFocus();
                ClearSelection();
                return true;
            }
            
            // 2. Safe version compatibility fallback: if the mouse action isn't a click, it's a movement/hover.
            // Swallow it here so it doesn't propagate down to background components like FileExplorer.
            if (mouse.motion != ftxui::Mouse::Pressed && mouse.motion != ftxui::Mouse::Released) {
                return true; 
            }
        }
    }
    // ----------------------------------------------

    const std::string& input = event.input();
    if (input == "\x1A" || input == "Ctrl+Z") {
        Undo();
        return true;
    }
    if (input == "\x19" || input == "Ctrl+Y") {
        Redo();
        return true;
    }

    if (event.is_character()) {
        // Insert incoming characters at the current cursor position.
        SaveSnapshotForTyping(event.input());
        if (HasSelection()) {
            DeleteSelectionWithoutSnapshot();
        }
        text_lines_[cursor_y_].insert(cursor_x_, event.input());
        cursor_x_ += event.input().size();
        ClearSelection();
        UpdateScroll();
        return true;
    }

    if (event == ftxui::Event::Backspace) {
        EndTypingGroup();
        if (HasSelection()) {
            DeleteSelection();
            return true;
        }

        // Delete the character before the cursor or join with the previous line.
        if (cursor_x_ > 0) {
            SaveSnapshot();
            text_lines_[cursor_y_].erase(cursor_x_ - 1, 1);
            cursor_x_--;
        } else if (cursor_y_ > 0) {
            SaveSnapshot();
            cursor_x_ = text_lines_[cursor_y_ - 1].size();
            text_lines_[cursor_y_ - 1] += text_lines_[cursor_y_];
            text_lines_.erase(text_lines_.begin() + cursor_y_);
            cursor_y_--;
        }
        ClearSelection();
        UpdateScroll();
        return true;
    }

    if (event == ftxui::Event::Delete) {
        EndTypingGroup();
        if (HasSelection()) {
            DeleteSelection();
            return true;
        }

        if (cursor_x_ < text_lines_[cursor_y_].size()) {
            SaveSnapshot();
            text_lines_[cursor_y_].erase(cursor_x_, 1);
        } else if (cursor_y_ + 1 < text_lines_.size()) {
            SaveSnapshot();
            text_lines_[cursor_y_] += text_lines_[cursor_y_ + 1];
            text_lines_.erase(text_lines_.begin() + static_cast<std::ptrdiff_t>(cursor_y_ + 1));
        }
        ClearSelection();
        UpdateScroll();
        return true;
    }

    if (event == ftxui::Event::Return) {
        EndTypingGroup();
        SaveSnapshot();
        if (HasSelection()) {
            DeleteSelectionWithoutSnapshot();
        }

        // Split the active line at the current cursor position.
        std::string next_line = text_lines_[cursor_y_].substr(cursor_x_);
        text_lines_[cursor_y_].erase(cursor_x_);
        text_lines_.insert(text_lines_.begin() + cursor_y_ + 1, std::move(next_line));
        cursor_y_++;
        cursor_x_ = 0;
        ClearSelection();
        UpdateScroll();
        return true;
    }

    NavigationAction action = NavigationAction::Left;
    const bool extend_selection = IsShiftNavigationEvent(event, &action);
    if (extend_selection || IsNavigationEvent(event, &action)) {
        EndTypingGroup();
        if (extend_selection) {
            BeginSelection();
        }

        switch (action) {
            case NavigationAction::Left:
                MoveCursorLeft();
                break;
            case NavigationAction::Right:
                MoveCursorRight();
                break;
            case NavigationAction::Up:
                MoveCursorUp();
                break;
            case NavigationAction::Down:
                MoveCursorDown();
                break;
            case NavigationAction::Home:
                MoveCursorHome();
                break;
            case NavigationAction::End:
                MoveCursorEnd();
                break;
            case NavigationAction::WordLeft:
                MoveCursorToPreviousWord();
                break;
            case NavigationAction::WordRight:
                MoveCursorToNextWord();
                break;
        }

        ClampCursorToBuffer();
        if (!extend_selection) {
            ClearSelection();
        }
        UpdateScroll();
        return true;
    }

    return ComponentBase::OnEvent(event);
}

bool EditorComponent::Focusable() const {
    return true;
}

size_t EditorComponent::VisibleHeight() const {
    if (editor_box_.y_max >= editor_box_.y_min) {
        return static_cast<size_t>(
            std::max(1, editor_box_.y_max - editor_box_.y_min + 1));
    }
    return 1;
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

void EditorComponent::UpdateScroll() {
    const size_t visible_height = VisibleHeight();
    if (cursor_y_ < scroll_y_) {
        scroll_y_ = cursor_y_;
    } else if (cursor_y_ >= scroll_y_ + visible_height) {
        scroll_y_ = cursor_y_ - visible_height + 1;
    }

    if (text_lines_.size() <= visible_height) {
        scroll_y_ = 0;
        return;
    }

    const size_t max_scroll = text_lines_.size() - visible_height;
    scroll_y_ = std::min(scroll_y_, max_scroll);
}

bool EditorComponent::IsWordCharacter(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

bool EditorComponent::IsCharacterSelected(size_t x, size_t y) const {
    if (!HasSelection()) {
        return false;
    }

    auto [start, end] = OrderedSelection(
        {selection_anchor_x_, selection_anchor_y_},
        {cursor_x_, cursor_y_},
        text_lines_);
    const Position character_position{x, y};
    return !PositionLess(character_position, start) &&
        PositionLess(character_position, end);
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
    if (cursor_y_ > 0) {
        cursor_y_--;
        cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
    }
}

void EditorComponent::MoveCursorDown() {
    if (cursor_y_ + 1 < text_lines_.size()) {
        cursor_y_++;
        cursor_x_ = std::min(cursor_x_, text_lines_[cursor_y_].size());
    }
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

EditorComponent::EditorState EditorComponent::CurrentState() const {
    return {
        text_lines_,
        static_cast<int>(cursor_x_),
        static_cast<int>(cursor_y_),
    };
}

void EditorComponent::ApplyState(const EditorState& state) {
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

    EditorState state = CurrentState();
    if (!undo_stack_.empty() &&
        undo_stack_.back().lines == state.lines &&
        undo_stack_.back().cursor_x == state.cursor_x &&
        undo_stack_.back().cursor_y == state.cursor_y) {
        redo_stack_.clear();
        return;
    }

    undo_stack_.push_back(std::move(state));
    if (undo_stack_.size() > kMaxHistory) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
}

void EditorComponent::SaveSnapshotForTyping(const std::string& input) {
    const bool boundary =
        input.size() == 1 &&
        std::isspace(static_cast<unsigned char>(input.front()));

    if (!typing_group_active_ || boundary || HasSelection()) {
        SaveSnapshot();
    }

    typing_group_active_ = !boundary;
}

void EditorComponent::EndTypingGroup() {
    typing_group_active_ = false;
}

void EditorComponent::Undo() {
    EndTypingGroup();
    if (undo_stack_.empty()) {
        return;
    }

    redo_stack_.push_back(CurrentState());
    if (redo_stack_.size() > kMaxHistory) {
        redo_stack_.erase(redo_stack_.begin());
    }

    const EditorState state = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    ApplyState(state);
}

void EditorComponent::Redo() {
    EndTypingGroup();
    if (redo_stack_.empty()) {
        return;
    }

    undo_stack_.push_back(CurrentState());
    if (undo_stack_.size() > kMaxHistory) {
        undo_stack_.erase(undo_stack_.begin());
    }

    const EditorState state = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    ApplyState(state);
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
