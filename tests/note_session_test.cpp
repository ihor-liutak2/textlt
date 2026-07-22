#include "notes/note_session.hpp"

#include <cassert>
#include <string>

namespace textlt::utils {

bool IsUtf8ContinuationByte(char character) {
    return (static_cast<unsigned char>(character) & 0xC0U) == 0x80U;
}

size_t PreviousUtf8CodepointStart(const std::string& text, size_t index) {
    if (index == 0) return 0;
    --index;
    while (index > 0 && IsUtf8ContinuationByte(text[index])) --index;
    return index;
}

size_t NextUtf8CodepointStart(const std::string& text, size_t index) {
    if (index >= text.size()) return text.size();
    ++index;
    while (index < text.size() && IsUtf8ContinuationByte(text[index])) ++index;
    return index;
}

} // namespace textlt::utils

int main() {
    namespace notes = textlt::notes;
    notes::NoteDocument note = notes::MakeNewNote("test-device");
    notes::NoteSession session(&note);

    assert(session.Insert("Привіт"));
    assert(notes::BlockText(note.blocks[0]) == "Привіт");
    assert(session.Backspace());
    assert(notes::BlockText(note.blocks[0]) == "Приві");
    assert(session.Undo());
    assert(notes::BlockText(note.blocks[0]) == "Привіт");
    assert(session.Redo());
    assert(notes::BlockText(note.blocks[0]) == "Приві");

    session.MoveHome();
    session.MoveRight(true);
    session.MoveRight(true);
    assert(session.HasSelection());
    assert(session.ToggleMark(notes::NoteMark::Bold));
    assert((note.blocks[0].runs.front().marks & notes::MarkBit(notes::NoteMark::Bold)) != 0);

    session.MoveEnd();
    assert(session.Enter());
    assert(note.blocks.size() == 2);
    assert(session.SetBlockType(notes::NoteBlockType::CheckItem));
    assert(note.blocks[1].type == notes::NoteBlockType::CheckItem);
    assert(session.Insert("Перевірити"));
    assert(session.ToggleCheck());
    assert(note.blocks[1].checked);

    assert(session.Insert("\nДругий рядок"));
    assert(note.blocks.size() == 3);
    assert(notes::BlockText(note.blocks[2]) == "Другий рядок");

    session.SelectAll();
    assert(session.SelectedText().find("Приві") == 0);
    assert(session.DeleteSelection());
    assert(note.blocks.size() == 1);
    assert(notes::BlockText(note.blocks[0]).empty());
    return 0;
}
