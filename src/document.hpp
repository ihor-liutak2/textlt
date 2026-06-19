#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <optional>
#include <utility>
#include "history_manager.hpp"
#include "text_transformer.hpp"

namespace textlt {
    
    enum class DocumentType {
    PlainText,
    Bash,
    CMake,
    Cpp,
    Json,
    Markdown,
    Html,
    Css,
    Go,
    Js,
    Jsx,
    Ts,
    Tsx,
    Php,
    Blade,
    Rust,
    Java,
    Python,
    Xml
};

enum class LineEnding {
    LF,
    CRLF,
};

struct Selection {
    bool active = false;
    size_t anchor_x = 0;
    size_t anchor_y = 0;
};

struct Document {
    Document() = default;

    std::filesystem::path path = "Untitled";
    std::vector<std::string> lines{""};
    DocumentType type = DocumentType::PlainText;
    LineEnding line_ending = LineEnding::LF;
    size_t cursor_row = 0;
    size_t cursor_col = 0;
    Selection selection;
    bool is_dirty = false;
    
    HistoryManager history;

    Document(std::filesystem::path p);
    void SetPath(std::filesystem::path p);
    void Reset();
    void LoadContent(const std::string& content, std::filesystem::path p);
    std::string ToContent() const;
    std::string LineEndingLabel() const;
    std::string CurrentFilePath() const;
    size_t LineCount() const;
    std::string CurrentLineText() const;
    std::string TextFromCursor() const;
    void EnsureValidBuffer();
    void ClampCursor();
    void SetCursorPosition(size_t row, size_t column);
    void JumpToLine(size_t line_number);
    void MoveCursorHome();
    void MoveCursorEnd();
    void MoveCursorLeft();
    void MoveCursorRight();
    void MoveCursorUp();
    void MoveCursorDown();
    void MoveCursorToPreviousParagraph();
    void MoveCursorToNextParagraph();
    void MoveCursorToPreviousWord();
    void MoveCursorToNextWord();
    bool HasSelection() const;
    void BeginSelection();
    void ClearSelection();
    void SetSelectionAnchor(size_t row, size_t column);
    void SetSelectionActive(bool active);
    void SelectAll();
    std::string GetSelectedText() const;
    bool IsPositionSelected(size_t x, size_t y) const;
    bool DeleteSelection();
    bool DeleteSelectionWithoutSnapshot();
    HistoryManager::State CurrentState() const;
    void ApplyState(const HistoryManager::State& state);
    void SaveSnapshot();
    void SaveSnapshotForTyping(const std::string& input, bool has_selection);
    void EndTypingGroup();
    bool Undo();
    bool Redo();
    bool InsertText(const std::string& text);
    bool InsertCharacter(const std::string& input);
    bool InsertPairedCharacter(char opening, char closing);
    bool Backspace();
    bool DeleteForward();
    bool DeleteWordBackward();
    bool DeleteWordForward();
    bool DeleteCurrentLine();
    bool ConvertTabsToSpaces(size_t tab_size);
    bool Convert4To2Spaces();
    bool Convert2To4Spaces();
    bool IndentLines(size_t tab_size);
    bool OutdentLines(size_t tab_size);
    bool ToggleCase();
    bool MoveLineUp();
    bool MoveLineDown();
    bool DuplicateLine();
    bool MoveLinesUp();
    bool MoveLinesDown();
    bool DuplicateLines();
    std::string CommentPrefix() const;
    std::string Label() const;
};

} // namespace textlt
