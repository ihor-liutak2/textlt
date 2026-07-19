#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "history_manager.hpp"
#include "text_transformer.hpp"
#include "editor/editor_cursor_state.hpp"
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

struct DocumentTransformTarget {
    size_t start_row = 0;
    size_t start_column = 0;
    size_t end_row = 0;
    size_t end_column = 0;
    std::uint64_t buffer_version = 0;
    bool whole_document = false;
    std::string original_text;
};

class DocumentSession {
public:
    DocumentSession();
    explicit DocumentSession(std::filesystem::path p);
    DocumentSession(const DocumentSession& other);
    DocumentSession& operator=(const DocumentSession& other);
    DocumentSession(DocumentSession&& other) noexcept;
    DocumentSession& operator=(DocumentSession&& other) noexcept;

    TextBuffer buffer;
    HistoryManager history;

    std::vector<std::string>& lines;
    bool& is_dirty;

    std::filesystem::path path = "Untitled";
    DocumentType type = DocumentType::PlainText;
    LineEnding line_ending = LineEnding::LF;
    bool read_only = false;
    bool temporary = false;

    void SetActiveCursorState(EditorCursorState* cursor_state);
    void ClearActiveCursorState();
    EditorCursorState* ActiveCursorStatePointer() const;
    EditorCursorState& CursorState();
    const EditorCursorState& CursorState() const;
    size_t& CursorRow();
    size_t CursorRow() const;
    size_t& CursorCol();
    size_t CursorCol() const;
    Selection& SelectionState();
    const Selection& SelectionState() const;

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

    void RefreshLexer();
    bool IsMemoryOnly() const;
    std::string DisplayTitle() const;
    std::string TypeLabel() const;
    std::string Label() const;
    std::string LexerId() const;
    std::string CommentPrefix() const;
    std::string LineEndingText() const;

    bool GetTextProcessorTargetText(
        bool whole_document,
        std::string& text,
        std::string& error) const;
    bool ReplaceTextProcessorTargetText(
        bool whole_document,
        const std::string& text,
        std::string& error);
    bool CaptureAiTransformTarget(
        bool whole_document,
        DocumentTransformTarget& target,
        std::string& error) const;
    bool ReplaceAiTransformTarget(
        const DocumentTransformTarget& target,
        const std::string& text,
        std::string& error);

    static DocumentType DetermineDocumentType(const std::filesystem::path& path);

private:
    void BindFrom(const DocumentSession& other);
    EditorCursorState fallback_cursor_state_;
    EditorCursorState* active_cursor_state_ = &fallback_cursor_state_;
    HistoryManager::State CurrentTextProcessorState() const;
    void ClampTextProcessorCursor();
    void SelectWholeTextProcessorBuffer();
    void ClearTextProcessorSelection();
    std::string SelectedTextFromBuffer() const;
    bool DeleteTextProcessorSelection();
    bool InsertTextProcessorText(const std::string& text);
};

using EditorSession = DocumentSession;

} // namespace textlt
