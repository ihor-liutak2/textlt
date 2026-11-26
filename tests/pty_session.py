import fcntl
import os
import pty
import struct
import subprocess
import termios


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


def terminate_process(process, grace=0.5):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=grace)
