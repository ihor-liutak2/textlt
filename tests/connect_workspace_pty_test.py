import errno
import fcntl
import os
from pathlib import Path
import re
import select
import signal
import struct
import tempfile
import termios
import time

from pty_session import start_pty, terminate_process


ESC = "\x1b"
ANSI = re.compile(
    rb"\x1b\[[0-?]*[ -/]*[@-~]|\x1b[P\]][^\x07\x1b]*(?:\x07|\x1b\\)"
    rb"|\x1b[()][0-9A-Za-z]|\x1b."
)


class Terminal:
    def __init__(self, binary, root, environment, columns, rows):
        self.columns = columns
        self.height = rows
        self.rows = []
        self.process, self.fd = start_pty([binary], root, environment, columns, rows)
        try:
            self.read(timeout=8)
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.fd is None:
            return
        try:
            terminate_process(self.process)
        finally:
            os.close(self.fd)
            self.fd = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def read(self, timeout=2):
        chunks = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not select.select([self.fd], [], [], deadline - time.monotonic())[0]:
                break
            try:
                chunk = os.read(self.fd, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not chunk:
                break
            chunks.append(chunk)
        if chunks:
            plain = ANSI.sub(b"", b"".join(chunks)).decode("utf-8", errors="replace")
            rows = [row for row in re.split(r"[\r\n]+", plain) if row]
            if rows:
                self.rows = rows[-self.height:]
        return b"".join(chunks)

    def send(self, payload):
        os.write(self.fd, payload.encode())
        return self.read()

    def position(self, label):
        for row_index, row in enumerate(self.rows, 1):
            if label in row:
                return row.index(label) + 1, row_index
        raise AssertionError(f"Missing {label!r}:\n" + "\n".join(self.rows))

    def wait_for(self, label, timeout=8):
        deadline = time.monotonic() + timeout
        while not any(label in row for row in self.rows):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            self.read(timeout=remaining)
        return self.position(label)

    def click(self, label):
        x, y = self.position(label)
        self.click_at(x, y)

    def click_at(self, x, y):
        self.send(f"{ESC}[<0;{x};{y}M{ESC}[<0;{x};{y}m")

    def double_click(self, label):
        x, y = self.position(label)
        click = f"{ESC}[<0;{x};{y}M{ESC}[<0;{x};{y}m"
        self.send(click + click)

    def resize(self, columns, rows):
        self.columns = columns
        self.height = rows
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, columns, 0, 0))
        self.process.send_signal(signal.SIGWINCH)
        self.read(timeout=2)

    def wheel_down(self, x, y):
        self.send(f"{ESC}[<65;{x};{y}M")


def run(binary):
    with tempfile.TemporaryDirectory(prefix="textlt-connect-pty-") as temporary:
        root = Path(temporary)
        config = root / ".config"
        data = root / ".local" / "share"
        cache = root / ".cache"
        tools = root / "tools"
        tools.mkdir()
        clipboard = root / "clipboard.txt"
        clipboard.write_text("12345:clipboard-token\n")
        xclip = tools / "xclip"
        xclip.write_text(
            "#!/bin/sh\n"
            "case \"$*\" in\n"
            "  *' -o'*) cat \"$TEXTLT_TEST_CLIPBOARD\" ;;\n"
            "  *' -i'*) cat > \"$TEXTLT_TEST_CLIPBOARD\" ;;\n"
            "esac\n"
        )
        xclip.chmod(0o755)
        environment = dict(os.environ)
        environment.update(
            HOME=str(root),
            XDG_CONFIG_HOME=str(config),
            XDG_DATA_HOME=str(data),
            XDG_CACHE_HOME=str(cache),
            TERM="xterm-256color",
            LANG="C.UTF-8",
            PATH=f"{tools}:{os.defpath}",
            DISPLAY=":test",
            TEXTLT_TEST_CLIPBOARD=str(clipboard),
            GIT_CONFIG_NOSYSTEM="1",
            GIT_CONFIG_GLOBAL=str(root / "git-config"),
        )
        for key in ("SSH_CONNECTION", "SSH_TTY", "SSH_CLIENT", "WSL_INTEROP",
                    "WSL_DISTRO_NAME", "XDG_SESSION_TYPE", "WAYLAND_DISPLAY",
                    "WAYLAND_SOCKET"):
            environment.pop(key, None)

        with Terminal(str(Path(binary).resolve()), root, environment, 128, 36) as term:
            term.click("Options")
            term.send("\x1b[B" * 6 + "\r")
            local_x, local_y = term.wait_for("Local Connections")
            messenger_x, messenger_y = term.position("Messengers")
            conversation_x, conversation_y = term.position("Conversation")
            assert local_x < messenger_x < conversation_x, (
                (local_x, local_y), (messenger_x, messenger_y),
                (conversation_x, conversation_y), term.rows,
            )
            assert local_y == messenger_y == conversation_y, term.rows

            term.click("Local Connections")
            term.send("\t" * 5)
            term.send("\t" * 3)
            term.send("focus-draft")
            term.wait_for("focus-draft")
            term.click("Messengers")
            term.send("\t" * 3)
            term.send("-mouse")
            term.wait_for("focus-draft-mouse")
            term.click("Settings")
            term.wait_for("Telegram Settings")
            term.position("Test Connection")
            assert any("[x] Automatically reconnect" in row for row in term.rows)
            assert any("[ ] Start on application launch" in row for row in term.rows)
            term.click("Paste token")
            term.click("Show")
            term.wait_for("12345:clipboard-token")
            term.double_click("12345:clipboard-token")
            term.send("replacement-token")
            term.wait_for("replacement-token")
            term.send(ESC)
            term.wait_for("Messengers")

            term.resize(80, 36)
            local_x, local_y = term.wait_for("Local Connections")
            messenger_x, messenger_y = term.position("Messengers")
            conversation_x, conversation_y = term.position("Conversation")
            assert local_y == messenger_y < conversation_y
            assert conversation_x < local_x < messenger_x
            assert any("focus-draft-mouse" in row for row in term.rows)

            # A local-navigation wheel event must not scroll or clear the
            # selected conversation's draft.
            term.wheel_down(local_x + 4, local_y + 4)
            assert any("focus-draft-mouse" in row for row in term.rows)
            term.wheel_down(messenger_x + 4, messenger_y + 4)
            assert any("focus-draft-mouse" in row for row in term.rows)

            term.resize(32, 30)
            local_x, local_y = term.wait_for("Local Connections")
            messenger_x, messenger_y = term.position("Messengers")
            conversation_x, conversation_y = term.position("Conversation")
            assert local_y < messenger_y < conversation_y
            assert max(local_x, messenger_x, conversation_x) - min(
                local_x, messenger_x, conversation_x
            ) <= 2, ((local_x, local_y), (messenger_x, messenger_y),
                     (conversation_x, conversation_y), term.rows)
            assert any("focus-draft-mouse" in row for row in term.rows)


if __name__ == "__main__":
    import sys

    run(sys.argv[1])
