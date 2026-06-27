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

    Document vertical;
    vertical.lines = {u8"абвгд", u8"ёжзий", u8"abcde"};
    vertical.cursor_row = 0;
    vertical.cursor_col = std::string(u8"абв").size();
    vertical.MoveCursorDown();
    assert(vertical.cursor_row == 1);
    assert(vertical.cursor_col == std::string(u8"ёжз").size());
    vertical.MoveCursorDown();
    assert(vertical.cursor_row == 2);
    assert(vertical.cursor_col == 3);

    return 0;
}
