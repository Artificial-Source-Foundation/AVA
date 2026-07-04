#!/usr/bin/env python3
import argparse
import fcntl
import os
import pathlib
import pty
import select
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time


SKIP = 77


def enabled(value: str | None) -> bool:
    return value is not None and value.lower() in {"1", "true", "yes", "on"}


def set_winsize(fd: int, rows: int, cols: int) -> None:
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def strip_csi(text: bytes) -> bytes:
    out = bytearray()
    index = 0
    while index < len(text):
        if text[index:index + 2] == b"\x1b[":
            index += 2
            while index < len(text) and not (0x40 <= text[index] <= 0x7E):
                index += 1
            if index < len(text):
                index += 1
            continue
        if text[index:index + 2] == b"\x1b]":
            index += 2
            while index < len(text):
                if text[index:index + 2] == b"\x1b\\":
                    index += 2
                    break
                if text[index] == 0x07:
                    index += 1
                    break
                index += 1
            continue
        out.append(text[index])
        index += 1
    return bytes(out)


def read_until(master_fd: int, process: subprocess.Popen[bytes], predicate, label: str, timeout: float = 10.0) -> bytes:
    deadline = time.monotonic() + timeout
    captured = bytearray()
    while time.monotonic() < deadline:
        if predicate(bytes(captured)):
            return bytes(captured)
        ready, _, _ = select.select([master_fd], [], [], 0.1)
        if ready:
            try:
                chunk = os.read(master_fd, 8192)
            except OSError:
                chunk = b""
            if chunk:
                captured.extend(chunk)
                continue
        if process.poll() is not None:
            raise RuntimeError(
                f"AVA exited before {label} with code {process.returncode}\n"
                f"captured:\n{strip_csi(bytes(captured)).decode(errors='replace')}"
            )
    raise RuntimeError(
        f"timed out waiting for {label}\n"
        f"captured:\n{strip_csi(bytes(captured)).decode(errors='replace')}"
    )


def terminate_process(process: subprocess.Popen[bytes], master_fd: int) -> None:
    if process.poll() is not None:
        return
    try:
        os.write(master_fd, b"\x04")
        process.wait(timeout=3)
        return
    except Exception:
        pass
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=3)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ava", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    if not enabled(os.environ.get("AVA_TUI_KITTY_IMAGE_SMOKE")):
        print("skipping Kitty image PTY smoke; set AVA_TUI_KITTY_IMAGE_SMOKE=1 to run")
        return SKIP

    ava_exe = pathlib.Path(args.ava).resolve()
    if not ava_exe.exists():
        raise RuntimeError(f"AVA executable does not exist: {ava_exe}")

    root = pathlib.Path(args.root).resolve()
    if root.exists():
        if root.name != "tui-kitty-image-smoke":
            raise RuntimeError(f"refusing to clear unexpected smoke root: {root}")
        shutil.rmtree(root)
    workspace = root / "workspace"
    home = root / "home"
    config = root / "config"
    state = root / "state"
    data = root / "data"
    for path in (workspace, home, config, state, data):
        path.mkdir(parents=True, exist_ok=True)
    (workspace / "AGENTS.md").write_text("kitty image smoke context\n", encoding="utf-8")
    (workspace / "screen.png").write_bytes(b"\x89PNG\r\n\x1a\nava-kitty-image")

    env = os.environ.copy()
    env.update(
        {
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(config),
            "XDG_STATE_HOME": str(state),
            "XDG_DATA_HOME": str(data),
            "TERM": "xterm-256color",
            "TERM_PROGRAM": "kitty",
            "KITTY_WINDOW_ID": "1",
            "COLORTERM": "truecolor",
        }
    )
    env.pop("TMUX", None)
    env.pop("TMUX_PANE", None)
    env.pop("NO_COLOR", None)
    env.pop("AVA_TUI_THEME", None)

    master_fd, slave_fd = pty.openpty()
    set_winsize(slave_fd, 28, 100)
    process = subprocess.Popen(
        [str(ava_exe)],
        cwd=workspace,
        env=env,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        start_new_session=True,
    )
    os.close(slave_fd)

    try:
        read_until(master_fd, process, lambda data: b"Type a message" in data or b"live session" in data,
                   "initial TUI frame")
        os.write(master_fd, b"/attach screen.png\r")
        expected_payload = b"iVBORw0KGgphdmEta2l0dHktaW1hZ2U="
        captured = read_until(
            master_fd,
            process,
            lambda data: b"\x1b_Ga=T,f=100,q=2,C=1" in data and expected_payload in data and b"\x1b\\" in data,
            "Kitty graphics emission",
        )
        visible = strip_csi(captured)
        if b"attached image" not in visible or b"screen.png" not in visible or b"preview kitty" not in visible:
            raise RuntimeError(
                "attached image metadata did not remain visible with Kitty preview\n"
                f"captured:\n{visible.decode(errors='replace')}"
            )
        if b"\x1b_Ga=T,f=100,q=2,C=1" not in captured:
            raise RuntimeError("Kitty graphics sequence did not use the expected transmit command")
        if expected_payload not in captured:
            raise RuntimeError("Kitty graphics sequence did not include the smoke PNG payload")
        os.write(master_fd, b"\x04")
        process.wait(timeout=5)
        return 0
    finally:
        terminate_process(process, master_fd)
        os.close(master_fd)


if __name__ == "__main__":
    sys.exit(main())
