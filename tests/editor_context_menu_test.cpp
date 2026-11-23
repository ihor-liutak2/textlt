#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "context_menu_component.hpp"
#include "editor/document_session.hpp"
#include "editor/editor_viewport.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/screen/screen.hpp"

namespace {

using namespace textlt;
using namespace ftxui;

void Check(bool passed, const char* message) {
    if (passed) return;
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

Event MouseEvent(Mouse::Button button, Mouse::Motion motion, int x, int y) {
    Mouse mouse{};
    mouse.button = button;
    mouse.motion = motion;
    mouse.x = x;
    mouse.y = y;
    return Event::Mouse("", mouse);
}

Screen Draw(ContextMenuComponent& menu, int width, int height) {
    auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(height));
    Render(screen, dbox({filler(), menu.RenderIn({width, height})}));
    return screen;
}

void TestMenu() {
    Theme theme;
    std::string executed;
    ContextMenuComponent menu(theme, [&](const std::string& id) { executed = id; });
    const std::vector<ContextMenuComponent::Item> items = {
        {"undo", "Undo", false}, {}, {"copy", "Copy"},
        {"paste", "Paste", false}, {"select_all", "Select All"},
    };

    menu.Open(27, 19, items);
    auto screen = Draw(menu, 28, 20);
    Check(screen.PixelAt(14, 13).character == "┌" &&
          screen.PixelAt(27, 19).character == "┘", "Menu must fit at both edges");
    Check(menu.OnEvent(MouseEvent(Mouse::Left, Mouse::Pressed, 16, 14)),
          "Disabled rows must consume clicks");
    Check(menu.IsOpen() && executed.empty(), "Disabled Undo executed");
    Check(menu.OnEvent(Event::ArrowDown) && menu.OnEvent(Event::Return),
          "Menu did not handle keyboard navigation");
    Check(!menu.IsOpen() && executed == "select_all",
          "Navigation should skip disabled items and close after Enter");

    menu.Open(-4, -6, items);
    screen = Draw(menu, 28, 20);
    Check(screen.PixelAt(0, 0).character == "┌", "Menu must fit at top and left edges");
    Check(menu.OnEvent(MouseEvent(Mouse::Left, Mouse::Pressed, 2, 3)) &&
          menu.OnEvent(MouseEvent(Mouse::Left, Mouse::Released, 2, 3)) &&
          executed == "copy" && !menu.IsOpen(), "Clicking Copy should execute and close");

    menu.Open(3, 2, items);
    Draw(menu, 28, 20);
    Check(!menu.OnEvent(MouseEvent(Mouse::Left, Mouse::Pressed, 0, 0)) && !menu.IsOpen(),
          "Outside click should dismiss and reach its normal target");
    menu.Open(3, 2, items);
    Check(menu.OnEvent(Event::Escape) && !menu.IsOpen(), "Escape did not dismiss the menu");
    menu.Open(3, 2, items);
    Check(!menu.OnEvent(Event::Character('x')) && !menu.IsOpen(),
          "Ordinary typing should dismiss the menu and reach the editor");

    menu.Open(10, 10, items);
    menu.OnEvent(Event::End);
    screen = Draw(menu, 7, 4);
    Check(screen.PixelAt(0, 0).character == "┌" &&
          screen.PixelAt(6, 3).character == "┘", "Short terminals must keep the popup in bounds");
    Check(screen.PixelAt(2, 2).character == "S" &&
          menu.OnEvent(Event::Return) && executed == "select_all",
          "Keyboard selection should scroll into view on a short terminal");
}

void Select(DocumentSession& document, size_t start_row, size_t start_col,
            size_t end_row, size_t end_col) {
    document.SetCursorPosition(end_row, end_col);
    document.SetSelectionAnchor(start_row, start_col);
    document.SetSelectionActive(true);
}

void TestEditing() {
    DocumentSession document;
    document.LoadContent(u8"prefix aBéΣß! suffix\n  MiXeD\nkeep", "context.txt");
    const std::string original = document.ToContent();
    const std::string mixed = u8"aBéΣß!";
    Select(document, 0, 7, 0, 7 + mixed.size());
    Check(document.ChangeCase(transform::CaseMode::Toggle), "Toggle Case was a no-op");
    Check(document.GetSelectedText() == u8"AbÉσSS!", "Toggle must change each cased character");
    Check(document.lines.front() == u8"prefix AbÉσSS! suffix", "Toggle changed unselected text");
    Check(document.Undo() && document.ToContent() == original && document.GetSelectedText() == mixed,
          "One Undo should restore content and the original selection");
    Check(document.Redo() && document.GetSelectedText() == u8"AbÉσSS!",
          "One Redo should restore the toggle");

    Select(document, 1, 7, 0, 7);
    Check(document.ChangeCase(transform::CaseMode::Upper), "Uppercase was a no-op");
    Check(document.lines[0] == u8"prefix ABÉΣSS! SUFFIX" && document.lines[1] == "  MIXED",
          "Uppercase must support reversed multiline selections");
    Check(document.ChangeCase(transform::CaseMode::Lower), "Lowercase was a no-op");
    Check(document.GetSelectedText() == u8"abéσss! suffix\n  mixed",
          "Lowercase must retain the exact selection");
    document.ClearSelection();
    Check(!document.ChangeCase(transform::CaseMode::Upper), "Uppercase requires a selection");

    document.LoadContent("first\n\tsecond\nthird", "context.txt");
    Select(document, 2, 0, 0, 1);
    const std::string before_indent = document.ToContent();
    Check(document.IndentLines(2) && document.lines[0] == "  first" &&
          document.lines[1] == "  \tsecond" && document.lines[2] == "third",
          "Indent must respect width and exclude an unselected end line");
    Check(document.CanOutdentLines() && document.OutdentLines(2) &&
          document.ToContent() == before_indent, "Outdent must remove one configured level");
    Check(document.Undo() && document.lines[0] == "  first" && document.lines[1] == "  \tsecond",
          "One Undo should revert all outdented lines");
    Check(document.Undo() && document.ToContent() == before_indent,
          "One Undo should revert all indented lines");
    Check(document.Redo() && document.lines[0] == "  first" && document.lines[1] == "  \tsecond",
          "One Redo should restore all indented lines");
    document.read_only = true;
    const auto before_read_only = document.ToContent();
    Check(!document.IndentLines(2) && !document.CanOutdentLines() &&
          !document.OutdentLines(2) && !document.ChangeCase(transform::CaseMode::Toggle) &&
          !document.Undo() && !document.Redo() && document.ToContent() == before_read_only,
          "Read-only documents must reject edits and history mutations");
}

void TestEditorMouse() {
    EditorViewport viewport;
    viewport.SetBox({0, 19, 0, 3});
    DocumentSession document;
    document.lines = {"abcdef", "1", "2", "3", "4", "5", "6"};
    document.SetActiveCursorState(&viewport.CursorState());
    const auto mouse = [&](Mouse::Button button, Mouse::Motion motion, int x, int y) {
        return viewport.HandleMouseEvent(document, nullptr, MouseEvent(button, motion, x, y));
    };
    Check(mouse(Mouse::Left, Mouse::Pressed, 1, 0) &&
          mouse(Mouse::Left, Mouse::Moved, 4, 0) &&
          mouse(Mouse::Left, Mouse::Released, 4, 0) &&
          document.GetSelectedText() == "bcd", "Left drag selection regressed");
    Check(!mouse(Mouse::Right, Mouse::Pressed, 0, 2) &&
          document.GetSelectedText() == "bcd", "Viewport right press must preserve selection");
    Check(mouse(Mouse::WheelDown, Mouse::Pressed, 1, 1) && viewport.scroll_y > 0,
          "Normal editor scrolling regressed");
}

} // namespace

int main() {
    TestMenu();
    TestEditing();
    TestEditorMouse();
}
