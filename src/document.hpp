#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <optional>
#include <utility>
#include "history_manager.hpp"
#include "text_transformer.hpp"
#include "editor/document_session.hpp"
#include "editor/text_buffer.hpp"

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

struct Document {
    Document();
    Document(const Document& other);
    Document& operator=(const Document& other);
    Document(Document&& other) noexcept;
    Document& operator=(Document&& other) noexcept;
    explicit Document(std::filesystem::path p);

    TextBuffer buffer;
    DocumentSession session;

    std::filesystem::path path = "Untitled";
    std::vector<std::string>& lines;
    DocumentType type = DocumentType::PlainText;
    LineEnding line_ending = LineEnding::LF;
    size_t& cursor_row;
    size_t& cursor_col;
    Selection& selection;
    bool& is_dirty;
    bool read_only = false;
    bool temporary = false;
    
    HistoryManager history;

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

private:
    void BindFrom(const Document& other);
};

} // namespace textlt
