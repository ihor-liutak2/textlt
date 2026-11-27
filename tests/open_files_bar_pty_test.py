import fcntl
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import sys
import tempfile
import termios

from editor_context_menu_pty_test import ESC, HEIGHT, WIDTH, Terminal, mouse


TAB_ROWS = (5, 6)


def close_tab(term, name):
    x, y = term.position(name)
    assert y in TAB_ROWS, term.rows
    right_edge = term.rows[y - 1].index("┊×", x - 1) + 2
    term.click_at((right_edge, y))


def click_save(term):
    _, y = term.position("Choose how to continue.")
    x = term.rows[y - 1].index(" Save ") + 2
    term.click_at((x, y))


def assert_tab(term, name, dirty=False):
    x, y = term.position(name)
    assert y in TAB_ROWS, term.rows
    assert term.rows[y - 1][x - 3:x - 1] == ("* " if dirty else "  "), term.rows


def assert_three_rows(term):
    assert term.rows[TAB_ROWS[1]].strip() == "", term.rows
    editor_row = term.rows[TAB_ROWS[1] + 1]
    assert "● 1:" in editor_row, term.rows
    assert not any(character in editor_row for character in "┌┐┏┓┃━"), term.rows


def resize(term, width):
    fcntl.ioctl(term.fd, termios.TIOCSWINSZ, struct.pack("HHHH", HEIGHT, width, 0, 0))
    os.kill(term.process.pid, signal.SIGWINCH)
    return term.send(mouse(35, (1, 1)))


def test_mouse_and_panes(term, files):
    assert_three_rows(term)
    assert term.position("epsilon.txt")[1] == TAB_ROWS[1]
    term.click("alpha.txt")
    term.position("● 1: alpha.txt")
    term.send("x")
    assert_tab(term, "alpha.txt", dirty=True)
    term.send("\x13")
    assert_tab(term, "alpha.txt")
    assert files[0].read_text().startswith("x"), files[0].read_text()

    term.send("y")
    term.click("bravo.txt")
    close_tab(term, "alpha.txt")
    term.position("Save changes to alpha.txt before closing?")
    term.click("Cancel")
    term.position("● 1: bravo.txt")
    assert_tab(term, "alpha.txt", dirty=True)
    close_tab(term, "alpha.txt")
    term.position("Save changes to alpha.txt before closing?")
    click_save(term)
    term.position("● 1: bravo.txt")
    assert all("alpha.txt" not in row for row in term.rows[TAB_ROWS[0] - 1:TAB_ROWS[1]]), term.rows
    assert files[0].read_text().startswith("xy"), files[0].read_text()
    assert files[1].read_text() == "content Bravo\n"

    term.click("charlie.txt")
    term.send("z")
    term.click("delta.txt")
    close_tab(term, "charlie.txt")
    term.click("Don't Save")
    term.position("● 1: delta.txt")
    assert files[2].read_text() == "content Charlie\n"

    term.click("Options")
    term.click_at(term.wait_for("View Layout..."))
    term.click_at(term.wait_for("Two columns"))
    term.send(ESC)
    term.position("● 1: delta.txt")
    term.send(f"{ESC}[1;3C")
    term.position("● 2:")
    term.click("epsilon.txt")
    term.position("● 2: epsilon.txt")
    term.position("○ 1: delta.txt")
    term.click("content Delta")
    term.position("● 1: delta.txt")
    close_tab(term, "bravo.txt")
    term.position("● 1: delta.txt")
    term.position("○ 2: epsilon.txt")

    term.click("File")
    term.click(" New ")
    term.wait_for("● 1: Untitled")
    term.send("draft contents")
    term.click("delta.txt")
    close_tab(term, "Untitled")
    term.position("Save changes to Untitled before closing?")
    click_save(term)
    term.wait_for("file name")
    term.send(ESC)
    assert_tab(term, "Untitled", dirty=True)
    term.position("● 1: delta.txt")
    close_tab(term, "Untitled")
    click_save(term)
    term.click_at(term.wait_for("file name"))
    term.send("draft.txt\r")
    term.position("● 1: delta.txt")
    assert (files[0].parent / "draft.txt").read_text() == "draft contents"
    assert all("Untitled" not in row for row in term.rows[TAB_ROWS[0] - 1:TAB_ROWS[1]])

    term.send("\x02o")
    term.wait_for(str(files[-1]))
    term.send(ESC)


def test_resize_and_theme(term, files):
    assert_three_rows(term)
    term.click("Options")
    term.click_at(term.wait_for("Theme..."))
    term.send(f"{ESC}[B\r")
    term.wait_for("Theme: GitHub Light")
    frame = resize(term, 43)
    assert b"\x1b[48;2;3;102;214m" in frame, frame
    assert_tab(term, "epsilon.txt")
    assert term.position("+2 more")[1] == TAB_ROWS[1]
    term.click("+2 more")
    term.position(files[2].name)
    term.send("\r")
    assert_tab(term, "charlie.txt")
    assert all("epsilon.txt" not in row for row in term.rows[TAB_ROWS[0] - 1:TAB_ROWS[1]]), term.rows

    resize(term, 18)
    assert_tab(term, "charlie.txt")
    assert term.position("+4 more")[1] == TAB_ROWS[1]
    assert term.rows[TAB_ROWS[0] - 1].endswith("┊×"), term.rows

    resize(term, WIDTH)
    assert_three_rows(term)
    assert_tab(term, "charlie.txt")
    assert_tab(term, "epsilon.txt")
    assert all("+2 more" not in row for row in term.rows), term.rows


def run(binary):
    with tempfile.TemporaryDirectory(prefix="textlt-tabs-") as directory:
        root = Path(directory)
        files = []
        for name in ("Alpha", "Bravo", "Charlie", "Delta", "Epsilon"):
            path = root / f"{name.lower()}.txt"
            path.write_text(f"content {name}\n")
            files.append(path)

        for index, check in enumerate((test_mouse_and_panes, test_resize_and_theme)):
            home = root / f"home-{index}"
            config = home / ".config" / "textlt"
            themes = config / "themes"
            themes.mkdir(parents=True)
            source_themes = Path(__file__).resolve().parents[1] / "themes"
            for filename in ("blueprint.json", "github_light.json"):
                shutil.copyfile(source_themes / filename, themes / filename)
            (config / "config.json").write_text(json.dumps({
                "active_theme_name": "Blueprint",
                "show_file_explorer": True,
            }))
            env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(config.parent),
                       XDG_DATA_HOME=str(home / "data"),
                       XDG_CACHE_HOME=str(home / "cache"),
                       XDG_STATE_HOME=str(home / "state"),
                       TERM="xterm-256color", COLORTERM="truecolor")
            with Terminal(binary, root, files, env) as term:
                check(term, files)


if __name__ == "__main__":
    run(str(Path(sys.argv[1]).resolve()))
