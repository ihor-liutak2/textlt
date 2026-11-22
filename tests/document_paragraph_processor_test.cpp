#include <iostream>
#include <cassert>
#include <string>

#include "editor/document_session.hpp"

int main() {
    textlt::DocumentSession session;
    session.LoadContent("First line\nSecond line\n\nThird paragraph", "test.txt");
    session.SetCursorPosition(1, 3);

    std::cerr << "Paragraph replacement\n";
    std::string paragraph;
    std::string error;
    assert(session.GetCurrentParagraphText(paragraph, error));
    assert(paragraph == "Second line");

    assert(session.ReplaceCurrentParagraphText("Edited paragraph", error));
    assert(session.ToContent() == "First line\nEdited paragraph\n\nThird paragraph");
    assert(session.Undo());
    assert(session.ToContent() == "First line\nSecond line\n\nThird paragraph");

    session.SetCursorPosition(2, 0);
    std::cerr << "Blank paragraph\n";
    error.clear();
    assert(!session.GetCurrentParagraphText(paragraph, error));
    assert(error.find("empty paragraph") != std::string::npos);

    return 0;
}
