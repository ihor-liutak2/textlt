#include <iostream>
#include <cassert>
#include <string>

#include "editor/document_session.hpp"

int main() {
    textlt::DocumentSession session;
    session.LoadContent("First line\nSecond line\n\nThird paragraph", "test.txt");
    session.SetCursorPosition(1, 3);

    std::cerr << "Paragraph replacement\n";
    textlt::DocumentTransformTarget paragraph;
    std::string error;
    assert(session.CaptureAiTransformTarget(false, paragraph, error));
    assert(paragraph.original_text == "Second line");
    assert(!paragraph.whole_document);

    assert(session.ReplaceAiTransformTarget(paragraph, "Edited paragraph", error));
    assert(session.ToContent() == "First line\nEdited paragraph\n\nThird paragraph");
    assert(session.Undo());
    assert(session.ToContent() == "First line\nSecond line\n\nThird paragraph");

    std::cerr << "Stale transformation target\n";
    textlt::DocumentTransformTarget stale;
    assert(session.CaptureAiTransformTarget(true, stale, error));
    session.SetCursorPosition(0, 0);
    assert(session.InsertText("Changed "));
    assert(!session.ReplaceAiTransformTarget(stale, "Replacement", error));
    assert(error.find("changed") != std::string::npos);

    session.SetCursorPosition(2, 0);
    std::cerr << "Blank target\n";
    textlt::DocumentTransformTarget blank;
    error.clear();
    assert(!session.CaptureAiTransformTarget(false, blank, error));
    assert(error.find("empty paragraph") != std::string::npos);

    textlt::DocumentSession visible_cursor;
    visible_cursor.LoadContent(
        "First paragraph\n\nSecond paragraph line one\nSecond paragraph line two\n\nThird paragraph",
        "cursor-test.txt");
    // The session fallback cursor intentionally remains in the first paragraph.
    visible_cursor.SetCursorPosition(0, 0);
    std::cerr << "Paragraph at cursor\n";
    textlt::DocumentTransformTarget cursor_paragraph;
    error.clear();
    assert(visible_cursor.CaptureAiTransformTargetAt(
        3, 7, false, cursor_paragraph, error));
    assert(cursor_paragraph.original_text == "Second paragraph line two");
    assert(cursor_paragraph.start_row == 3);
    assert(cursor_paragraph.end_row == 3);

    return 0;
}
