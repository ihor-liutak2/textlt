import json
import os
import re
from pathlib import Path
import sys
import tempfile

from connect_workspace_pty_test import ESC, Terminal


def mouse(button, x, y, suffix="M"):
    return f"{ESC}[<{button};{x};{y}{suffix}"


def field_position(term, label, value):
    _, row = term.position(label)
    return term.rows[row - 1].index(value) + 1, row


def run(binary):
    with tempfile.TemporaryDirectory(prefix="textlt-irc-settings-") as directory:
        root = Path(directory).resolve()
        config = root / ".config" / "textlt"
        config.mkdir(parents=True)
        settings = config / "irc-settings.json"
        profiles = dict(server="localhost", port=6667, tls=False, nickname="tester", username="tester")
        settings.write_text(json.dumps({"connections": [
            dict(profiles, id="fixture", name="Local Hub", server_password="masked-secret"),
            dict(profiles, id="other", name="Other Server"),
        ]}))
        clipboard = root / "clipboard"
        tool = root / "xclip"
        tool.write_text(
            '#!/bin/sh\ncase "$3" in\n'
            ' -i) cat > "$TEXTLT_TEST_CLIPBOARD";;\n'
            ' -o) cat "$TEXTLT_TEST_CLIPBOARD";;\nesac\n'
        )
        tool.chmod(0o755)
        env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(config.parent),
                   XDG_DATA_HOME=str(root / ".local"), XDG_STATE_HOME=str(root / ".state"),
                   XDG_CACHE_HOME=str(root / ".cache"), TERM="xterm-256color", LANG="C.UTF-8",
                   PATH=f"{root}:{os.defpath}", DISPLAY=":test", TEXTLT_TEST_CLIPBOARD=str(clipboard))
        for key in ("TEXTLT_TERMINAL_CLIPBOARD", "SSH_CONNECTION", "SSH_TTY", "SSH_CLIENT",
                    "WSL_INTEROP", "WSL_DISTRO_NAME", "XDG_SESSION_TYPE", "WAYLAND_DISPLAY",
                    "WAYLAND_SOCKET"):
            env.pop(key, None)

        with Terminal(str(Path(binary).resolve()), root, env, 128, 36) as term:
            term.click("Options")
            term.send((ESC + "[B") * 6 + "\r")
            term.click_at(*term.wait_for("IRC Settings"))
            term.wait_for("Saved connections")
            term.position("Test Connection")
            assert "masked-secret" not in "\n".join(term.rows)

            x, y = field_position(term, "Connection name", "Hub")
            click = mouse(0, x, y) + mouse(0, x, y, "m")
            term.send(click + click + "Room")
            term.wait_for("Local Room")

            x, y = field_position(term, "Connection name", "Local")
            term.send(mouse(0, x, y) + mouse(32, x + 5, y) + mouse(0, x + 5, y, "m") + "\x03")
            assert clipboard.read_text() == "Local", "Copy did not use the mouse selection."
            clipboard.write_text("Team")
            term.send("\x16")
            term.wait_for("Team Room")
            # FTXUI positions the visible cursor relative to the end of the frame.
            cursor = rb"\x1b\[(\d+)D\x1b\[\?25h"
            first = re.findall(cursor, term.send(ESC + "[D"))
            second = re.findall(cursor, term.send(ESC + "[D"))
            assert first and second and int(second[-1]) == int(first[-1]) + 1, (first, second)

            term.resize(39, 20)
            test_position = term.wait_for("Test Connection")
            cancel_position = term.position("Cancel")
            term.send("\t" * 11)
            term.wait_for("Start on application launch")
            assert term.position("Test Connection") == test_position
            assert term.position("Cancel") == cancel_position
            term.click(" Save ")
            term.resize(128, 36)
            term.click_at(*term.wait_for("Team Room"))
            term.wait_for("Join Channel")
            term.click("Channel or nickname")
            term.send("#offline")
            term.click("Join Channel")
            assert sum("#offline" in row for row in term.rows) == 1
            term.click("#offline")
            term.send(ESC + "[F" + "\x7f" * len("#offline") + "FixtureNick")
            term.click("Open Private Chat")
            term.click_at(*term.wait_for("FixtureNick"))
            term.wait_for("Type a message")
            assert all("Send File" not in row for row in term.rows)

        saved = json.loads(settings.read_text())["connections"]
        assert [p["name"] for p in saved] == ["Team Room", "Other Server"]
        assert saved[0]["server_password"] == "masked-secret"


if __name__ == "__main__":
    run(sys.argv[1])
