#include <cassert>
#include <string>

#include "editor/document_session.hpp"

int main() {
    textlt::DocumentSession session;
    session.LoadContent("First line\nSecond line\n\nThird paragraph", "test.txt");
    session.SetCursorPosition(1, 3);

    textlt::DocumentTransformTarget paragraph;
    std::string error;
    assert(session.CaptureAiTransformTarget(false, paragraph, error));
    assert(paragraph.original_text == "First line\nSecond line");
    assert(!paragraph.whole_document);

    assert(session.ReplaceAiTransformTarget(paragraph, "Edited paragraph", error));
    assert(session.ToContent() == "Edited paragraph\n\nThird paragraph");
    assert(session.Undo());
    assert(session.ToContent() == "First line\nSecond line\n\nThird paragraph");

    textlt::DocumentTransformTarget stale;
    assert(session.CaptureAiTransformTarget(true, stale, error));
    session.SetCursorPosition(0, 0);
    assert(session.InsertText("Changed "));
    assert(!session.ReplaceAiTransformTarget(stale, "Replacement", error));
    assert(error.find("changed") != std::string::npos);

    session.SetCursorPosition(2, 0);
    textlt::DocumentTransformTarget blank;
    error.clear();
    assert(!session.CaptureAiTransformTarget(false, blank, error));
    assert(error.find("empty paragraph") != std::string::npos);
    return 0;
}
