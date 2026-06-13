#pragma once

#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor_config.hpp"
#include "theme.hpp"

namespace textlt {

class EditorComponent : public ftxui::ComponentBase {
public:
    EditorComponent(EditorConfig* config, const Theme* theme);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    void SaveToFile(const std::string& path);
    void LoadFromFile(const std::string& path);
    void NewFile(const std::string& path);
    const std::string& CurrentFilePath() const;
    int GetCursorRow() const;
    int GetCursorCol() const;
    void SetBottomOverlayRows(size_t rows);
    void JumpToLine(int line_number);

    bool HasSelection() const;
    std::string GetSelectedText() const;
    void DeleteSelection();
    void ClearSelection();
    void InsertText(const std::string& text);
    void ConvertTabsToSpaces();
    void Undo();
    void Redo();
    void HighlightMatches(const std::string& query);
    void ClearSearchHighlights();
    void JumpToNextMatch();
    void JumpToPreviousMatch();
    void ExecuteReplaceNext(const std::string& query, const std::string& replacement);
    void ExecuteReplaceAll(const std::string& query, const std::string& replacement);
    size_t SearchMatchCount() const;
    size_t CurrentSearchMatchIndex() const;
    
    std::string GetCurrentLineText() const;
    void DeleteCurrentLine();

private:
    struct EditorState {
        std::vector<std::string> lines;
        int cursor_x = 0;
        int cursor_y = 0;
    };

    struct SearchMatch {
        size_t x = 0;
        size_t y = 0;
        size_t length = 0;
    };

    static constexpr size_t kMaxHistory = 100;

    size_t VisibleHeight() const;
    size_t VisibleTextWidth() const;
    size_t LineNumberWidth() const;
    std::string LineNumberText(size_t line_index, size_t width) const;
    void UpdateScroll();
    static bool IsWordCharacter(char character);
    bool IsCharacterSelected(size_t x, size_t y) const;
    const SearchMatch* SearchMatchAt(size_t x, size_t y) const;
    bool IsActiveSearchMatch(const SearchMatch& match) const;
    void BeginSelection();
    void ClampCursorToBuffer();
    void MoveCursorHome();
    void MoveCursorEnd();
    void MoveCursorLeft();
    void MoveCursorRight();
    void MoveCursorUp();
    void MoveCursorDown();
    void MoveCursorToPreviousWord();
    void MoveCursorToNextWord();
    EditorState CurrentState() const;
    void ApplyState(const EditorState& state);
    void SaveSnapshot();
    void SaveSnapshotForTyping(const std::string& input);
    void EndTypingGroup();
    void DeleteSelectionWithoutSnapshot();
    size_t FindMatchAtOrAfterCursor() const;
    void MoveCursorToSearchMatch(const SearchMatch& match);

    std::vector<std::string> text_lines_;
    std::vector<EditorState> undo_stack_;
    std::vector<EditorState> redo_stack_;
    std::vector<SearchMatch> search_matches_;
    std::string current_filepath_ = "untitled.txt";
    EditorConfig* config_ = nullptr;
    const Theme* theme_ = nullptr;
    size_t cursor_x_ = 0;
    size_t cursor_y_ = 0;
    size_t scroll_x_ = 0;
    size_t scroll_y_ = 0;
    size_t selection_anchor_x_ = 0;
    size_t selection_anchor_y_ = 0;
    bool has_selection_ = false;
    bool typing_group_active_ = false;
    bool is_dirty_ = false;
    bool mouse_selecting_ = false;
    size_t current_search_match_ = 0;
    size_t bottom_overlay_rows_ = 0;
    ftxui::Box editor_box_;
};

} // namespace textlt
