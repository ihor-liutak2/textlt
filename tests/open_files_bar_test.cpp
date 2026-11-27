#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "editor/document_session.hpp"
#include "editor/document_workspace.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/screen/screen.hpp"
#include "open_files_bar.hpp"
#include "path_utils.hpp"

namespace {

using namespace textlt;
using namespace ftxui;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

struct Fixture {
    DocumentWorkspace workspace;
    Theme theme;
    size_t activated = 99;
    size_t closed = 99;
    OpenFilesBar bar{workspace, theme,
        [this](size_t index) { activated = index; },
        [this](size_t index) { closed = index; }};

    void Add(const std::string& path) {
        auto document = std::make_shared<DocumentSession>();
        document->SetPath(PathFromUtf8(path));
        workspace.AddSession(std::move(document));
    }

    Screen Draw(int width) {
        auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(8));
        Render(screen, vbox({text("Menu"), bar.Render(), text("Body"), filler()}));
        return screen;
    }

    bool Click(int x, int y) {
        Mouse mouse{};
        mouse.button = Mouse::Left;
        mouse.motion = Mouse::Pressed;
        mouse.x = x;
        mouse.y = y;
        return bar.OnEvent(Event::Mouse("", mouse));
    }
};

std::string Row(Screen& screen, int y) {
    std::string line;
    for (int x = 0; x < screen.dimx(); ++x) line += screen.PixelAt(x, y).character;
    return line;
}

void TestWidthAndRows() {
    Fixture f;
    auto empty = f.Draw(73);
    Check(Row(empty, 1) == "" && Row(empty, 2) == "" &&
        Row(empty, 3) == "" && Row(empty, 4).find("Body") == 0,
        "An empty bar should still occupy three rows");
    for (const auto* name : {"one.txt", "two.txt", "three.txt", "four.txt"}) f.Add(name);

    auto screen = f.Draw(73);
    for (int y : {1, 2}) {
        Check(screen.PixelAt(34, y).character == "×" &&
            screen.PixelAt(72, y).character == "×", "Expected two 35-column tabs per row");
        for (int x = 35; x < 38; ++x) {
            Check(screen.PixelAt(x, y).character.empty(), "Expected three columns between tabs");
        }
    }
    Check(Row(screen, 3) == "", "Third row must stay empty");

    screen = f.Draw(72);
    Check(screen.PixelAt(33, 1).character == "×" && screen.PixelAt(70, 2).character == "×",
        "A one-column resize should select 34-column tabs");
    Check(f.Click(38, 2) && f.activated == 3, "Second-row tab should activate its document");
    Check(f.Click(33, 1) && f.closed == 0 && f.activated == 3,
        "Close should have its own hit target");
    Check(f.Click(35, 1) && f.closed == 0 && f.activated == 3,
        "The gap must be outside tab hitboxes");
}

void TestOverflow() {
    Fixture f;
    for (const auto* name : {"one.txt", "two.txt", "three.txt", "four.txt", "five.txt"}) f.Add(name);
    f.workspace.SetActiveSessionIndex(4);
    auto screen = f.Draw(43);
    Check(Row(screen, 1).find("one.txt") != std::string::npos &&
        Row(screen, 1).find("two.txt") != std::string::npos &&
        Row(screen, 2).find("five.txt") != std::string::npos &&
        Row(screen, 2).find("+2 more") != std::string::npos,
        "Overflow should leave the active document visible and reserve the last slot");
    Check(f.Click(23, 2) && f.bar.IsFileListOpen(), "Overflow must open the hidden list");
    Check(f.bar.OnEvent(Event::Return) && f.activated == 2 && !f.bar.IsFileListOpen(),
        "Overflow should offer the displaced document first");

    screen = f.Draw(13);
    Check(Row(screen, 1).find("five") != std::string::npos &&
        screen.PixelAt(12, 1).character == "×" &&
        Row(screen, 2).find("+4 more") == 0, "Narrow rendering must fit one tab per row");
    screen = f.Draw(1);
    Check(screen.PixelAt(0, 1).background_color == f.theme.modal_selected_item_bg &&
        screen.PixelAt(0, 2).character == "+", "One-column terminal must retain active and overflow slots");

    f.workspace.RemoveSession(0);
    screen = f.Draw(43);
    Check(Row(screen, 2).find("more") == std::string::npos &&
        screen.PixelAt(42, 2).character == "×", "Closing a file should reclaim the overflow slot");
}

void TestFormattingAndTheme() {
    Fixture f;
    f.theme.modal_selected_item_bg = Color::Magenta;
    f.theme.modal_selected_item_fg = Color::Yellow;
    f.theme.foreground = Color::Green;
    f.Add(u8"e\u0301漢字漢字漢字漢字漢字漢字.txt");
    f.Add("left/report.txt");
    f.Add("east/report.txt");
    f.Add("line\nbreak.txt");
    f.workspace.SessionAt(0)->is_dirty = true;
    f.workspace.SetActiveSessionIndex(0);

    auto screen = f.Draw(43);
    Check(Row(screen, 1).find(u8"* e\u0301漢字") == 0 && Row(screen, 1).find("…") != std::string::npos &&
        Row(screen, 1).find(".txt ┊×") != std::string::npos &&
        screen.PixelAt(19, 1).character == "×", "Unicode truncation must preserve width and extension");
    Check(Row(screen, 1).find("left/report.txt") != std::string::npos &&
        Row(screen, 2).find("east/report.txt") != std::string::npos,
        "Equal filenames need visible parent directory names");
    Check(Row(screen, 2).find("line?break.txt") != std::string::npos &&
        screen.PixelAt(42, 2).character == "×", "Control characters must fit on one row");
    for (int x = 0; x < 20; ++x) {
        Check(screen.PixelAt(x, 1).background_color == f.theme.modal_selected_item_bg &&
            screen.PixelAt(x, 1).foreground_color == f.theme.modal_selected_item_fg,
            "Active colors must fill the entire tab, including the close button");
    }
    Check(screen.PixelAt(23, 1).character == " " &&
        screen.PixelAt(42, 1).foreground_color == f.theme.foreground,
        "Inactive tabs should reserve a blank marker and use the file-list color");

    f.workspace.SessionAt(0)->is_dirty = false;
    screen = f.Draw(43);
    Check(screen.PixelAt(0, 1).character == " ", "The saved marker must clear on the next frame");
}

} // namespace

int main() {
    TestWidthAndRows();
    TestOverflow();
    TestFormattingAndTheme();
}
