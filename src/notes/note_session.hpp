#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "notes/note_document.hpp"

namespace textlt::notes {

struct NotePosition {
    size_t block = 0;
    size_t byte = 0;
};

inline bool operator==(const NotePosition& left, const NotePosition& right) {
    return left.block == right.block && left.byte == right.byte;
}
inline bool operator!=(const NotePosition& left, const NotePosition& right) { return !(left == right); }

class NoteSession {
public:
    explicit NoteSession(NoteDocument* note = nullptr);
    void SetNote(NoteDocument* note);
    NoteDocument* Note() { return note_; }
    const NoteDocument* Note() const { return note_; }
    const NotePosition& Cursor() const { return cursor_; }
    bool HasSelection() const { return selection_anchor_.has_value() && *selection_anchor_ != cursor_; }
    std::pair<NotePosition, NotePosition> Selection() const;
    uint8_t ActiveMarks() const { return active_marks_; }
    bool Dirty() const { return dirty_; }
    void MarkSaved() { dirty_ = false; }

    bool Insert(const std::string& text);
    bool Backspace();
    bool DeleteForward();
    bool Enter();
    bool MoveLeft(bool selecting = false);
    bool MoveRight(bool selecting = false);
    bool MoveUp(bool selecting = false);
    bool MoveDown(bool selecting = false);
    void MoveHome(bool selecting = false);
    void MoveEnd(bool selecting = false);
    void SelectAll();
    std::string SelectedText() const;
    bool DeleteSelection();
    bool ToggleMark(NoteMark mark);
    bool ClearFormatting();
    bool SetBlockType(NoteBlockType type);
    bool ToggleCheck();
    bool Indent(int delta);
    bool Undo();
    bool Redo();
    static bool Before(const NotePosition& left, const NotePosition& right);

private:
    void BeginSelection(bool selecting);
    void ClampCursor();
    void PushUndo();
    void Changed();
    bool DeleteRange(NotePosition start, NotePosition end);
    void ApplyMarks(NoteBlock& block, size_t start, size_t end, uint8_t mask, bool add);
    void InsertRunText(NoteBlock& block, size_t byte, const std::string& text, uint8_t marks);
    void NormalizeRuns(NoteBlock& block);
    uint8_t MarksAt(const NoteBlock& block, size_t byte) const;
    void SplitBlockAtCursor();
    void UpdateActiveMarks();

    NoteDocument* note_ = nullptr;
    NotePosition cursor_;
    std::optional<NotePosition> selection_anchor_;
    uint8_t active_marks_ = 0;
    bool dirty_ = false;
    std::vector<NoteDocument> undo_;
    std::vector<NoteDocument> redo_;
};

} // namespace textlt::notes
