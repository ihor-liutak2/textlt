#include "editor/document_session.hpp"
#include "editor_utils.hpp"

#include <cassert>
#include <string>

using textlt::DocumentSession;
using textlt::EditorCursorState;

int main() {
    const std::string one = u8"один";
    const std::string two = u8"два";
    const std::string three = u8"три";
    const std::string line = one + " " + two + " " + three;

    assert(textlt::utils::Utf8DisplayWidth(u8"українська") == 10);
    assert(textlt::utils::Utf8ByteIndexAtDisplayColumn(u8"українська мова", 0, 10) ==
           std::string(u8"українська").size());
    const auto wrapped = textlt::utils::BuildUtf8WrapSegments(u8"один два три", 5);
    assert(wrapped.size() == 3);
    assert(textlt::utils::Utf8DisplayWidth(
               u8"один два три", wrapped[0].start, wrapped[0].end) == 5);

    assert(textlt::utils::FindWordDeleteStart(line, line.size()) ==
           line.size() - three.size());
    assert(textlt::utils::FindWordDeleteEnd(line, 0) == one.size());

    DocumentSession doc;
    EditorCursorState doc_cursor;
    doc.SetActiveCursorState(&doc_cursor);
    doc.lines = {line};
    doc.CursorRow() = 0;
    doc.CursorCol() = line.size();
    doc.BeginSelection();
    doc.MoveCursorToPreviousWord();

    assert(doc.CursorCol() == line.size() - three.size());
    assert(doc.GetSelectedText() == three);

    doc.ClearSelection();
    doc.MoveCursorToPreviousWord();
    assert(doc.CursorCol() == one.size() + 1);

    doc.CursorCol() = 0;
    doc.MoveCursorToNextWord();
    assert(doc.CursorCol() == one.size() + 1);

    DocumentSession vertical;
    EditorCursorState vertical_cursor;
    vertical.SetActiveCursorState(&vertical_cursor);
    vertical.lines = {u8"абвгд", u8"ёжзий", u8"abcde"};
    vertical.CursorRow() = 0;
    vertical.CursorCol() = std::string(u8"абв").size();
    vertical.MoveCursorDown();
    assert(vertical.CursorRow() == 1);
    assert(vertical.CursorCol() == std::string(u8"ёжз").size());
    vertical.MoveCursorDown();
    assert(vertical.CursorRow() == 2);
    assert(vertical.CursorCol() == 3);

    return 0;
}
