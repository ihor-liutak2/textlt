import base64
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
import unicodedata


ESC = "\x1b"
WIDTH, HEIGHT = 128, 36
ANSI = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]|\x1b[P\]][^\x07\x1b]*(?:\x07|\x1b\\)"
                 rb"|\x1b[()][0-9A-Za-z]|\x1b.")


def mouse(button, point, suffix="M"):
    x, y = point
    return f"{ESC}[<{button};{x};{y}{suffix}"


class Terminal:
    def __init__(self, binary, root, path, environment):
        self.rows = []
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(root)
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", HEIGHT, WIDTH, 0, 0))
            os.execve(binary, [binary, str(path)], environment)
        self.read(timeout=5, settle=0.8)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        try:
            os.kill(self.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        os.waitpid(self.pid, 0)
        os.close(self.fd)

    def read(self, timeout=2, settle=0.12):
        chunks = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            wait = min(settle, deadline - time.monotonic()) if chunks else deadline - time.monotonic()
            if not select.select([self.fd], [], [], max(0, wait))[0]:
                break
            chunks.append(os.read(self.fd, 65536))
        if chunks:
            output = b"".join(chunks)
            plain = ANSI.sub(b"", output).decode("utf-8").replace("\x00", "")
            rows = [row for row in re.split(r"[\r\n]+", plain) if row]
            if len(rows) >= HEIGHT:
                self.rows = rows[-HEIGHT:]
            return output
        return b""

    def send(self, payload):
        os.write(self.fd, payload.encode())
        return self.read()

    def position(self, text):
        for row_number, row in enumerate(self.rows, 1):
            if text in row:
                prefix = row[:row.index(text)]
                columns = sum(2 if unicodedata.east_asian_width(char) in ("W", "F") else 1
                              for char in prefix if not unicodedata.combining(char))
                return columns + 1, row_number
        raise AssertionError(f"Missing {text!r}:\n" + "\n".join(self.rows))

    def click(self, text):
        return self.click_at(self.position(text))

    def wait_for(self, text, timeout=5):
        deadline = time.monotonic() + timeout
        while not any(text in row for row in self.rows):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            self.read(timeout=remaining)
        return self.position(text)

    def click_at(self, point, button=0):
        return self.send(mouse(button, point) + mouse(button, point, "m"))

    def drag(self, start, end):
        self.send(mouse(0, start) + mouse(32, end) + mouse(0, end, "m"))

    def open_menu(self, point):
        self.click_at(point, button=2)
        self.position("Copy File Path")

    def require_closed(self):
        assert all("Copy File Path" not in row for row in self.rows), self.rows


def test_editing(term, path, clipboard):
    x, y = term.position("left aBc right")
    term.click_at((x, 1), button=2)
    term.require_closed()

    term.drag((x + 5, y), (x + 8, y))
    term.open_menu((x + 13, y))
    term.click("Toggle Case")
    term.position("left AbC right")
    term.require_closed()
    term.open_menu((x + 13, y))
    term.send("\r")  # Undo is the first available item.
    term.position("left aBc right")
    term.open_menu((x + 13, y))
    term.send("\r")  # Redo is now the first available item.
    term.position("left AbC right")

    term.drag((x + 5, y), (x + 8, y))
    term.open_menu((x + 13, y))
    term.click("UPPERCASE")
    term.position("left ABC right")
    term.open_menu((x + 13, y))
    term.click("lowercase")
    term.position("left abc right")
    term.open_menu((x + 13, y))
    term.click(" Copy ")
    assert clipboard.read_text() == "abc", repr(clipboard.read_text())
    term.open_menu((x + 13, y))
    term.click(" Cut ")
    assert clipboard.read_text() == "abc", repr(clipboard.read_text())
    term.position("left  right")
    term.open_menu((x + 13, y))
    term.click(" Paste ")
    term.position("left abc right")

    term.open_menu((x + 13, y))
    term.click("Copy File Path")
    assert clipboard.read_text() == str(path)
    term.drag((x, y), (x + 4, y + 1))
    term.open_menu((x + 20, y))
    term.click("Indent Right")
    assert "  left abc right" in term.rows[y - 1]
    assert "  second" in term.rows[y]
    term.open_menu((x + 20, y))
    term.click("Indent Left")
    assert "  left abc right" not in term.rows[y - 1]
    assert "  second" not in term.rows[y]

    term.drag((x + 5, y), (x + 8, y))
    term.open_menu((x + 13, y))
    term.click("Find Selection")
    term.position("1 match")
    term.send(ESC)
    x, y = term.position("left abc right")
    term.open_menu((WIDTH - 4, HEIGHT - 3))
    term.send(ESC)
    term.require_closed()
    term.open_menu((x + 20, y))
    term.click_at((x, y))
    term.require_closed()


def open_compare(term):
    term.click(" Tools ")
    term.click_at(term.wait_for("Git..."))
    # Opening the Git modal refreshes repository status before its first frame.
    term.click_at(term.wait_for("Compare"))
    term.click_at(term.wait_for("Open side by side"))
    term.wait_for("[RO]")
    # The new pane needs a repaint after its first layout pass.
    term.send(mouse(35, (1, 1)))


def test_read_only(term, clipboard):
    open_compare(term)
    x, y = term.position("left aBc right")
    term.drag((x + 5, y), (x + 8, y))
    term.click("old line")
    term.open_menu((x + 13, y))
    for label in (" Cut ", " Paste ", "Toggle Case", "Indent Right", "Copy File Path"):
        term.click(label)
        term.position("Copy File Path")
    term.click(" Copy ")
    assert clipboard.read_text() == "aBc"
    term.position("left aBc right")
    term.require_closed()


def require_compare_closed(term):
    term.wait_for("| Plain Text")
    frame = "\n".join(term.rows)
    assert "[RO]" not in frame and "↑ Prev" not in frame and "↓ Next" not in frame, frame
    term.position("left abc right")


def test_compare_exit(term):
    term.click(" Options ")
    term.click_at(term.wait_for("View Layout..."))
    term.click_at(term.wait_for("Three columns"))
    term.wait_for("Set pane 3")
    term.send(ESC)
    require_compare_closed(term)
    term.position("3: sample.txt")

    open_compare(term)
    term.click(" File ")
    term.click_at(term.wait_for("Close Git Compare"))
    require_compare_closed(term)
    term.position("3: sample.txt")

    open_compare(term)
    term.click(" File ")
    term.click_at(term.wait_for("Recent Files"))
    term.wait_for("Enter or double-click opens selected file")
    term.send("\r")
    require_compare_closed(term)
    term.position("3: sample.txt")

    open_compare(term)
    term.click(" Options ")
    term.click_at(term.wait_for("View Layout..."))
    term.click_at(term.wait_for(" Single "))
    term.send(ESC)
    require_compare_closed(term)
    assert "2: sample.txt" not in "\n".join(term.rows), term.rows


def test_terminal_clipboard(term):
    x, y = term.position("left aBc right")
    term.click_at((x + 5, y))
    term.open_menu((x + 13, y))
    output = term.click(" Paste ")
    assert b"\x1b]52;c;?\x1b\\" in output, "Paste did not query the terminal clipboard"
    encoded = base64.b64encode(b"terminal-").decode("ascii")
    term.send(f"{ESC}]52;c;{encoded}{ESC}\\")
    line = "left terminal-aBc right"
    x, y = term.position(line)
    term.click_at((x, y))
    output = term.send("\x03")  # Ctrl+C
    encoded = base64.b64encode(line.encode()).decode()
    assert f"{ESC}]52;c;{encoded}{ESC}\\".encode() in output, \
        "Copy without a selection did not send the current line"

    term.drag((x + 14, y), (x + 17, y))
    output = term.send("\x03")
    encoded = base64.b64encode(b"aBc").decode()
    assert f"{ESC}]52;c;{encoded}{ESC}\\".encode() in output, \
        "Copy with a selection did not send the selected text"

    term.drag((x + 14, y), (x + 17, y))
    output = term.send("\x18")  # Ctrl+X
    assert f"{ESC}]52;c;{encoded}{ESC}\\".encode() in output, \
        "Cut with a selection did not send the selected text"
    term.position("left terminal- right")
    term.send("\x1a")
    term.position(line)

    term.click_at((x, y))
    output = term.send("\x18")
    encoded = base64.b64encode(line.encode()).decode()
    assert f"{ESC}]52;c;{encoded}{ESC}\\".encode() in output, \
        "Cut without a selection did not send the current line"
    term.send("\x1a")
    term.position(line)


def test_osc52_multiline_utf8(term):
    x, y = term.position("éΣ")
    term.drag((x, y), (x + 6, y + 1))
    output = term.send("\x03")  # Ctrl+C
    encoded = base64.b64encode("éΣ\nsecond".encode()).decode()
    assert f"{ESC}]52;c;{encoded}{ESC}\\".encode() in output, \
        "OSC 52 did not preserve UTF-8 multiline selection"

    term.drag((x, y), (x + 6, y + 1))
    output = term.send("\x18")  # Ctrl+X
    assert f"{ESC}]52;c;{encoded}{ESC}\\".encode() in output, \
        "Cut did not transmit UTF-8 multiline selection"
    term.position("third")
    term.send("\x1a")
    term.position("second")


def test_failed_native_write(term):
    x, y = term.position("left aBc right")
    term.drag((x + 5, y), (x + 8, y))
    term.send("\x18")  # Ctrl+X
    term.position("left aBc right")


def test_failed_osc52_falls_back(binary, path, environment, clipboard):
    input_master, input_slave = pty.openpty()
    output_master, output_slave = pty.openpty()
    process = subprocess.Popen(
        [binary, str(path)], stdin=input_slave, stdout=output_slave,
        stderr=subprocess.DEVNULL, env=environment)
    os.close(input_slave)
    os.close(output_slave)
    os.close(output_master)
    clipboard.unlink(missing_ok=True)
    try:
        time.sleep(1)
        os.write(input_master, b"\x18")  # Cut the current line.
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not clipboard.exists():
            time.sleep(0.05)
        assert clipboard.exists(), "Failed OSC 52 transmission did not fall back to native clipboard"
        assert clipboard.read_text() == "left aBc right", \
            "Native fallback received incorrect current-line content"
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(input_master)


def run(binary):
    with tempfile.TemporaryDirectory(prefix="textlt-editor-context-") as directory:
        root = Path(directory).resolve()
        config = root / ".config" / "textlt"
        config.mkdir(parents=True)
        (config / "config.json").write_text('{"tab_size":2,"show_file_explorer":false}')
        path = root / "sample.txt"
        clipboard = root / "clipboard"
        tools = root / "bin"
        tools.mkdir()
        helper = tools / "xclip"
        helper.write_text('#!/bin/sh\ncase "$3" in\n -i) cat > "$TEXTLT_TEST_CLIPBOARD";;\n'
                          ' -o) cat "$TEXTLT_TEST_CLIPBOARD";;\nesac\n')
        helper.chmod(0o755)
        environment = dict(os.environ, HOME=directory, TERM="xterm-256color", COLORTERM="truecolor",
                           XDG_CONFIG_HOME=str(config.parent), XDG_DATA_HOME=str(root / "data"),
                           XDG_CACHE_HOME=str(root / "cache"), XDG_STATE_HOME=str(root / "state"),
                           PATH=f"{tools}:{os.defpath}", DISPLAY=":test",
                           TEXTLT_TEST_CLIPBOARD=str(clipboard), GIT_CONFIG_NOSYSTEM="1",
                           GIT_CONFIG_GLOBAL=str(root / "git-config"))
        for key in ("SSH_CONNECTION", "SSH_TTY", "SSH_CLIENT", "WSL_INTEROP", "WSL_DISTRO_NAME",
                    "XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
            environment.pop(key, None)
        git = shutil.which("git", path=environment["PATH"])
        if git:
            path.write_text("old line\nsecond\nthird\n")
            for args in (("init", "-q"), ("add", path.name),
                         ("-c", "user.name=Context Test", "-c", "user.email=test@example.invalid",
                          "commit", "-qm", "baseline")):
                subprocess.run([git, *args], cwd=root, env=environment, check=True)
        path.write_text("left aBc right\nsecond\nthird\n")
        with Terminal(binary, root, path, environment) as term:
            ordinary_frame = term.send(mouse(35, (1, 1)))
            assert b"left" in ANSI.sub(b"", ordinary_frame) and b"\x1b[48;2;40;92;135m" not in ordinary_frame
            test_editing(term, path, clipboard)
            if git:
                test_read_only(term, clipboard)
                test_compare_exit(term)
        with Terminal(binary, root, path, dict(environment, SSH_CONNECTION="fixture")) as term:
            test_terminal_clipboard(term)

        unicode_path = root / "unicode.txt"
        unicode_path.write_text("éΣ\nsecond\nthird\n")
        with Terminal(binary, root, unicode_path, dict(environment, SSH_CONNECTION="fixture")) as term:
            test_osc52_multiline_utf8(term)

        no_clipboard_environment = dict(environment)
        for key in ("DISPLAY", "SSH_CONNECTION", "SSH_TTY", "SSH_CLIENT", "WSL_INTEROP",
                    "WSL_DISTRO_NAME", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XDG_SESSION_TYPE"):
            no_clipboard_environment.pop(key, None)
        with Terminal(binary, root, path, no_clipboard_environment) as term:
            test_failed_native_write(term)

        fallback_environment = dict(environment, SSH_CONNECTION="fixture")
        with path.open("w") as source:
            source.write("left aBc right\n")
        test_failed_osc52_falls_back(
            str(Path(binary).resolve()), path, fallback_environment, clipboard)


if __name__ == "__main__":
    run(str(Path(sys.argv[1]).resolve()))
