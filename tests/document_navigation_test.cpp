#include "document.hpp"
#include "editor_utils.hpp"

#include <cassert>
#include <string>

using textlt::Document;

int main() {
    const std::string one = u8"один";
    const std::string two = u8"два";
    const std::string three = u8"три";
    const std::string line = one + " " + two + " " + three;

    assert(textlt::utils::FindWordDeleteStart(line, line.size()) ==
           line.size() - three.size());
    assert(textlt::utils::FindWordDeleteEnd(line, 0) == one.size());

    Document doc;
    doc.lines = {line};
    doc.cursor_row = 0;
    doc.cursor_col = line.size();
    doc.BeginSelection();
    doc.MoveCursorToPreviousWord();

    assert(doc.cursor_col == line.size() - three.size());
    assert(doc.GetSelectedText() == three);

    doc.ClearSelection();
    doc.MoveCursorToPreviousWord();
    assert(doc.cursor_col == one.size() + 1);

    doc.cursor_col = 0;
    doc.MoveCursorToNextWord();
    assert(doc.cursor_col == one.size() + 1);

    return 0;
}
