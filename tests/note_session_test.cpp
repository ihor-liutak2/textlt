#include "notes/note_session.hpp"

#include <cassert>
#include <cstdlib>
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

static void Check(bool condition) {
    if (!condition) std::abort();
}

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
    assert(session.ToggleHeading(2));
    assert(note.blocks[0].heading == 2);
    assert(note.blocks[0].type == notes::NoteBlockType::Paragraph);
    assert(session.ToggleHeading(2));
    assert(note.blocks[0].heading == 0);

    session.MoveEnd();
    const bool entered_replace_block = session.Enter();
    assert(entered_replace_block);
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

    // Replace All across blocks is a single undoable operation.
    session.SelectAll();
    const bool cleared = session.DeleteSelection();
    Check(cleared);
    const bool inserted_heading = session.Insert("Заголовок");
    Check(inserted_heading);
    session.MoveEnd();
    const bool toggled_heading = session.ToggleHeading(1);
    Check(toggled_heading);
    Check(note.blocks[0].heading == 1);
    const bool entered_heading = session.Enter();
    Check(entered_heading);
    Check(note.blocks.size() == 2);
    Check(note.blocks[0].heading == 1);
    Check(note.blocks[1].heading == 0);
    Check(note.blocks[1].type == notes::NoteBlockType::Paragraph);

    session.SetCursor({1, 0});
    const bool inserted_second_block = session.Insert("Повтор");
    Check(inserted_second_block);
    const std::string first_block = notes::BlockText(note.blocks[0]);
    const std::string second_block = notes::BlockText(note.blocks[1]);
    const bool replaced_ranges = session.ReplaceTextRanges({
        {0, 0, first_block.size()},
        {1, 0, second_block.size()},
    }, "Done");
    Check(replaced_ranges);
    Check(notes::BlockText(note.blocks[0]) == "Done");
    Check(notes::BlockText(note.blocks[1]) == "Done");
    // Replace All is one undoable operation across multiple note blocks.
    Check(session.Undo());
    Check(notes::BlockText(note.blocks[0]) == first_block);
    Check(notes::BlockText(note.blocks[1]) == second_block);
    return 0;
}
