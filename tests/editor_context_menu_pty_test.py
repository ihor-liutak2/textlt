import base64
import argparse
import base64
import errno
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
import tempfile
import termios
import time
import unicodedata


ESC = "\x1b"
WIDTH, HEIGHT = 128, 36
ANSI = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]|\x1b[P\]][^\x07\x1b]*(?:\x07|\x1b\\)"
                 rb"|\x1b[()][0-9A-Za-z]|\x1b.")


def terminate_and_reap(pid, grace=0.5):
    """Stop a child if needed, and always collect its exit status."""
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass

    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        try:
            waited, _ = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            return
        if waited == pid:
            return
        time.sleep(0.02)

    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass


def terminate_process(process, grace=0.5):
    if process.poll() is not None:
        process.wait()
        return
    process.terminate()
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


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
        try:
            self.read(timeout=5, settle=0.8)
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        if self.fd is None:
            return
        terminate_and_reap(self.pid)
        try:
            os.close(self.fd)
        finally:
            self.fd = None

    def read(self, timeout=2, settle=0.12):
        chunks = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            wait = min(settle, deadline - time.monotonic()) if chunks else deadline - time.monotonic()
            if not select.select([self.fd], [], [], max(0, wait))[0]:
                break
            try:
                chunk = os.read(self.fd, 65536)
            except OSError as error:
                # PTY masters report EIO on Linux/macOS after the slave closes.
                if error.errno == errno.EIO:
                    break
                raise
            if not chunk:
                break
            chunks.append(chunk)
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
    window_size = struct.pack("HHHH", HEIGHT, WIDTH, 0, 0)
    fcntl.ioctl(input_slave, termios.TIOCSWINSZ, window_size)
    fcntl.ioctl(output_slave, termios.TIOCSWINSZ, window_size)
    process = None
    try:
        process = subprocess.Popen(
            [binary, str(path)], stdin=input_slave, stdout=output_slave,
            stderr=subprocess.DEVNULL, env=environment)
        os.close(input_slave)
        input_slave = None
        os.close(output_slave)
        output_slave = None

        # Let startup output reach the PTY before disabling the output path.
        # Keeping stdin on its own PTY allows the test to trigger Cut after
        # stdout fails, which exercises native clipboard fallback.
        deadline = time.monotonic() + 5
        last_output = None
        received_output = False
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise AssertionError("TextLT exited before PTY output was disabled")
            readable, _, _ = select.select([output_master], [], [], 0.1)
            if readable:
                try:
                    data = os.read(output_master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not data:
                    break
                received_output = True
                last_output = time.monotonic()
            elif received_output and time.monotonic() - last_output >= 0.5:
                break
        assert received_output, "TextLT did not initialize its output PTY"

        os.close(output_master)
        output_master = None
        clipboard.unlink(missing_ok=True)
        os.write(input_master, b"\x18")  # Cut the current line.
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not clipboard.exists():
            if process.poll() is not None:
                raise AssertionError("TextLT exited before native clipboard fallback")
            time.sleep(0.05)
        assert clipboard.exists(), "Failed OSC 52 transmission did not fall back to native clipboard"
        assert clipboard.read_text() == "left aBc right", \
            "Native fallback received incorrect current-line content"
    finally:
        if process is not None:
            terminate_process(process)
        for fd in (input_master, input_slave, output_master, output_slave):
            if fd is not None:
                try:
                    os.close(fd)
                except OSError:
                    pass


SCENARIOS = (
    "editing",
    "read_only",
    "compare_exit",
    "terminal_clipboard",
    "osc52_multiline_utf8",
    "failed_native_write",
    "failed_osc52_fallback",
)


def run_scenario(binary, scenario):
    with tempfile.TemporaryDirectory(prefix=f"textlt-{scenario}-") as directory:
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
        environment = dict(os.environ)
        # Never inherit the opt-in from a developer or CI shell. These tests
        # enable OSC 52 only through SSH markers in scenarios that need it.
        environment.pop("TEXTLT_TERMINAL_CLIPBOARD", None)
        environment.update(
            HOME=directory, TERM="xterm-256color", COLORTERM="truecolor",
            XDG_CONFIG_HOME=str(config.parent), XDG_DATA_HOME=str(root / "data"),
            XDG_CACHE_HOME=str(root / "cache"), XDG_STATE_HOME=str(root / "state"),
            PATH=f"{tools}:{os.defpath}", DISPLAY=":test",
            TEXTLT_TEST_CLIPBOARD=str(clipboard), GIT_CONFIG_NOSYSTEM="1",
            GIT_CONFIG_GLOBAL=str(root / "git-config"))
        for key in ("SSH_CONNECTION", "SSH_TTY", "SSH_CLIENT", "WSL_INTEROP", "WSL_DISTRO_NAME",
                    "XDG_SESSION_TYPE", "WAYLAND_DISPLAY", "WAYLAND_SOCKET"):
            environment.pop(key, None)

        if scenario in {"read_only", "compare_exit"}:
            git = shutil.which("git", path=environment["PATH"])
            if not git:
                raise RuntimeError(f"Git is required for the {scenario} PTY scenario")
            path.write_text("old line\nsecond\nthird\n")
            for args in (("init", "-q"), ("add", path.name),
                         ("-c", "user.name=Context Test", "-c", "user.email=test@example.invalid",
                          "commit", "-qm", "baseline")):
                subprocess.run([git, *args], cwd=root, env=environment, check=True)

        current_line = "left abc right" if scenario == "compare_exit" else "left aBc right"
        path.write_text(f"{current_line}\nsecond\nthird\n")
        binary = str(Path(binary).resolve())

        if scenario == "editing":
            with Terminal(binary, root, path, environment) as term:
                ordinary_frame = term.send(mouse(35, (1, 1)))
                assert b"left" in ANSI.sub(b"", ordinary_frame) and \
                    b"\x1b[48;2;40;92;135m" not in ordinary_frame
                test_editing(term, path, clipboard)
        elif scenario == "read_only":
            with Terminal(binary, root, path, environment) as term:
                test_read_only(term, clipboard)
        elif scenario == "compare_exit":
            with Terminal(binary, root, path, environment) as term:
                test_compare_exit(term)
        elif scenario == "terminal_clipboard":
            with Terminal(binary, root, path, dict(environment, SSH_CONNECTION="fixture")) as term:
                test_terminal_clipboard(term)
        elif scenario == "osc52_multiline_utf8":
            unicode_path = root / "unicode.txt"
            unicode_path.write_text("éΣ\nsecond\nthird\n")
            with Terminal(binary, root, unicode_path,
                          dict(environment, SSH_CONNECTION="fixture")) as term:
                test_osc52_multiline_utf8(term)
        elif scenario == "failed_native_write":
            no_clipboard_environment = dict(environment)
            for key in ("DISPLAY", "SSH_CONNECTION", "SSH_TTY", "SSH_CLIENT", "WSL_INTEROP",
                        "WSL_DISTRO_NAME", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XDG_SESSION_TYPE"):
                no_clipboard_environment.pop(key, None)
            with Terminal(binary, root, path, no_clipboard_environment) as term:
                test_failed_native_write(term)
        elif scenario == "failed_osc52_fallback":
            test_failed_osc52_falls_back(
                binary, path, dict(environment, SSH_CONNECTION="fixture"), clipboard)
        else:
            raise ValueError(f"Unknown PTY scenario: {scenario}")


def run(binary, scenario):
    started = time.monotonic()
    print(f"[{scenario}] starting (elapsed 0.00s)", flush=True)
    try:
        run_scenario(binary, scenario)
    except BaseException:
        elapsed = time.monotonic() - started
        print(f"[{scenario}] failed after {elapsed:.2f}s", flush=True)
        raise
    elapsed = time.monotonic() - started
    print(f"[{scenario}] passed in {elapsed:.2f}s", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=lambda value: str(Path(value).resolve()))
    parser.add_argument("--scenario", choices=SCENARIOS, required=True)
    args = parser.parse_args()
    run(args.binary, args.scenario)
