#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor_config.hpp"
#include "history_manager.hpp"
#include "theme.hpp"

namespace textlt {

class EditorComponent : public ftxui::ComponentBase {
public:
    enum class LineEnding {
        LF,
        CRLF,
    };

    EditorComponent(EditorConfig* config, const Theme* theme);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override;

    void SaveToFile(const std::string& path);
    void LoadFromFile(const std::string& path);
    void NewFile(const std::string& path);
    const std::string& CurrentFilePath() const;
    bool IsDirty() const;
    LineEnding ActiveLineEnding() const;
    std::string ActiveLineEndingLabel() const;
    int GetCursorRow() const;
    int GetCursorCol() const;
    size_t GetLineCount() const;
    void SetBottomOverlayRows(size_t rows);
    void JumpToLine(int line_number);

    bool HasSelection() const;
    std::string GetSelectedText() const;
    void DeleteSelection();
    void ClearSelection();
    void InsertText(const std::string& text);
    void ConvertTabsToSpaces();
    void Convert4To2Spaces();
    void Convert2To4Spaces();
    void ToggleComment();
    void ToggleCase();
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
    std::optional<std::pair<int, int>> FindMatchingBracket() const;
    
    std::string GetCurrentLineText() const;
    void DeleteCurrentLine();

private:
    struct SearchMatch {
        size_t x = 0;
        size_t y = 0;
        size_t length = 0;
    };

    ftxui::Element RenderViewport();
    size_t VisibleHeight() const;
    size_t VisibleTextWidth() const;
    size_t LineNumberWidth() const;
    std::string LineNumberText(size_t line_index, size_t width) const;
    void UpdateScroll();
    bool HandleMouseEvent(ftxui::Event event);
    static bool IsWordCharacter(char character);
    bool IsCharacterSelected(size_t x, size_t y) const;
    std::optional<std::pair<int, int>> FindBracketNearCursor() const;
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
    void DeleteWordBackward();
    void DeleteWordForward();
    bool MoveLinesUp();
    bool MoveLinesDown();
    bool DuplicateLines();
    HistoryManager::State CurrentState() const;
    void ApplyState(const HistoryManager::State& state);
    void SaveSnapshot();
    void SaveSnapshotForTyping(const std::string& input);
    void EndTypingGroup();
    void DeleteSelectionWithoutSnapshot();
    bool HandleAutoPairCharacter(const std::string& input);
    bool HandleAutoIndentReturn();
    size_t FindMatchAtOrAfterCursor() const;
    void MoveCursorToSearchMatch(const SearchMatch& match);
    std::string GetCommentPrefix() const;

    std::vector<std::string> text_lines_;
    std::vector<SearchMatch> search_matches_;
    HistoryManager history_manager_;
    std::string current_filepath_ = "Untitled";
    LineEnding active_line_ending_ = LineEnding::LF;
    EditorConfig* config_ = nullptr;
    const Theme* theme_ = nullptr;
    size_t cursor_x_ = 0;
    size_t cursor_y_ = 0;
    size_t scroll_x_ = 0;
    size_t scroll_y_ = 0;
    size_t selection_anchor_x_ = 0;
    size_t selection_anchor_y_ = 0;
    bool has_selection_ = false;
    bool is_dirty_ = false;
    bool mouse_selecting_ = false;
    size_t current_search_match_ = 0;
    size_t bottom_overlay_rows_ = 0;
    ftxui::Box editor_box_;
};

} // namespace textlt
