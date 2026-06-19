#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <memory>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "editor_config.hpp"
#include "editor_input_controller.hpp"
#include "history_manager.hpp"
#include "theme.hpp"
#include "document.hpp"

namespace textlt {

    class EditorComponent : public ftxui::ComponentBase {
        friend class EditorInputController;

    public:
        EditorComponent(EditorConfig* config, const Theme* theme);

        ftxui::Element Render() override;
        bool OnEvent(ftxui::Event event) override;
        bool Focusable() const override;

        void SetDocument(std::shared_ptr<Document> doc);
        std::shared_ptr<Document> GetDocument() const;

        void SaveToFile(const std::string& path);
        void LoadFromFile(const std::string& path);
        void NewFile(const std::string& path);
        std::string GetAllText() const;

        // Accessors now delegate to doc_
        std::string CurrentFilePath() const;
        bool IsDirty() const;
        LineEnding ActiveLineEnding() const;
        std::string ActiveLineEndingLabel() const;
        size_t GetCursorRow() const;
        size_t GetCursorCol() const;
        size_t GetLineCount() const;
        std::string TextFromCursor() const;
        void SetBottomOverlayRows(size_t rows);
        void JumpToLine(size_t line_number);
        void SetCursorPosition(size_t row, size_t column);

        bool HasSelection() const;
        void SelectAll();
        std::string GetSelectedText() const;
        void DeleteSelection();
        void ClearSelection();
        void InsertText(const std::string& text);
        void ConvertTabsToSpaces();
        void Convert4To2Spaces();
        void Convert2To4Spaces();
        bool IndentLines();
        bool OutdentLines();
        void ToggleComment();
        void ToggleCase();
        void DeleteWordBackward();
        void DeleteWordForward();
        void Undo();
        void Redo();
        void HighlightMatches(const std::string& query);
        void ClearSearchHighlights();
        void JumpToNextMatch();
        void JumpToPreviousMatch();
        void ExecuteReplaceNext(const std::string& query, const std::string& replacement);
        void ExecuteReplaceAll(const std::string& query, const std::string& replacement);
        void ToggleSearchMatchCase();
        void ToggleSearchWholeWord();
        bool SearchMatchCase() const;
        bool SearchWholeWord() const;
        size_t SearchMatchCount() const;
        size_t CurrentSearchMatchIndex() const;
        std::optional<std::pair<size_t, size_t>> FindMatchingBracket() const;

        std::string GetCurrentLineText() const;
        void DeleteCurrentLine();
        bool HandleAutoIndentReturn();

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
        std::optional<std::pair<size_t, size_t>> FindBracketNearCursor() const;
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
        void MoveCursorPageUp();
        void MoveCursorPageDown();
        void MoveCursorToPreviousParagraph();
        void MoveCursorToNextParagraph();
        void MoveCursorToPreviousWord();
        void MoveCursorToNextWord();
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
        size_t FindMatchAtOrAfterCursor() const;
        void MoveCursorToSearchMatch(const SearchMatch& match);
        std::string GetCommentPrefix() const;

        // The shared reference to the active document
        std::shared_ptr<Document> doc_;

        // View-specific state
        EditorInputController input_controller_;
        std::vector<SearchMatch> search_matches_;
        EditorConfig* config_ = nullptr;
        const Theme* theme_ = nullptr;
        size_t scroll_x_ = 0;
        size_t scroll_y_ = 0;
        bool search_match_case_ = false;
        bool search_whole_word_ = false;
        bool mouse_selecting_ = false;
        bool is_dragging_scrollbar_ = false;
        size_t current_search_match_ = 0;
        size_t bottom_overlay_rows_ = 0;
        size_t drag_start_scroll_y_ = 0;
        int drag_start_y_ = 0;
        ftxui::Box editor_box_;
    };

} // namespace textlt
