import fcntl
import os
import pty
import struct
import subprocess
import termios
import time


def start_pty(args, cwd, env, columns, rows):
    # Wire up the PTY before starting the child. Python documents restrictions
    # on running Python code in the forkpty child on macOS.
    master, slave = pty.openpty()
    try:
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, columns, 0, 0))
        process = subprocess.Popen(
            args, cwd=cwd, env=env, stdin=slave, stdout=slave, stderr=slave,
            start_new_session=True,
        )
    except BaseException:
        os.close(master)
        raise
    finally:
        os.close(slave)
    return process, master


def _wait_for_exit(process, timeout):
    # Poll reaps the child directly and avoids the timed kqueue wait used by
    # Python 3.15 on macOS.
    deadline = time.monotonic() + timeout
    while process.poll() is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        time.sleep(min(0.02, remaining))
    return True


def terminate_process(process, grace=0.5):
    if process.poll() is not None:
        return
    process.terminate()
    if _wait_for_exit(process, grace):
        return
    process.kill()
    kill_timeout = 2
    if not _wait_for_exit(process, kill_timeout):
        raise subprocess.TimeoutExpired(process.args, kill_timeout)
