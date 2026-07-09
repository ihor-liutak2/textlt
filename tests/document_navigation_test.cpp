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

    DocumentSession selected_processor;
    EditorCursorState selected_processor_cursor;
    selected_processor.SetActiveCursorState(&selected_processor_cursor);
    selected_processor.lines = {"alpha beta gamma"};
    selected_processor.CursorRow() = 0;
    selected_processor.CursorCol() = 6;
    selected_processor.BeginSelection();
    selected_processor.CursorCol() = 10;

    std::string processor_error;
    assert(selected_processor.ReplaceTextProcessorTargetText(
        false,
        "BETA",
        processor_error));
    assert(selected_processor.lines.size() == 1);
    assert(selected_processor.lines[0] == "alpha BETA gamma");

    assert(selected_processor.Undo());
    assert(selected_processor.lines.size() == 1);
    assert(selected_processor.lines[0] == "alpha beta gamma");
    assert(selected_processor.HasSelection());
    assert(selected_processor.GetSelectedText() == "beta");
    assert(!selected_processor.Undo());

    assert(selected_processor.Redo());
    assert(selected_processor.lines.size() == 1);
    assert(selected_processor.lines[0] == "alpha BETA gamma");

    DocumentSession whole_document_processor;
    EditorCursorState whole_document_processor_cursor;
    whole_document_processor.SetActiveCursorState(&whole_document_processor_cursor);
    whole_document_processor.lines = {"one", "two"};
    whole_document_processor.CursorRow() = 0;
    whole_document_processor.CursorCol() = 0;

    assert(whole_document_processor.ReplaceTextProcessorTargetText(
        true,
        "ONE\nTWO",
        processor_error));
    assert(whole_document_processor.lines.size() == 2);
    assert(whole_document_processor.lines[0] == "ONE");
    assert(whole_document_processor.lines[1] == "TWO");

    assert(whole_document_processor.Undo());
    assert(whole_document_processor.lines.size() == 2);
    assert(whole_document_processor.lines[0] == "one");
    assert(whole_document_processor.lines[1] == "two");
    assert(!whole_document_processor.Undo());


    return 0;
}
